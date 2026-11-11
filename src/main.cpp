// Point d'entree du service : publie l'heure courante (synchronisee NTP) sur MQTT
// une fois par seconde, au format {"UnixTimeStamp":<secondes>}.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include "config.h"
#include "log.h"
#include "mqtt_publisher.h"
#include "ntp_client.h"

namespace {

using namespace std::chrono_literals;

// Drapeau global d'execution, arme a false par SIGINT/SIGTERM pour sortir proprement.
std::atomic<bool> g_running{true};

void handle_signal(int) { g_running = false; }

bool ntp_disabled(const std::string& server) {
  return server.empty() || server == "none" || server == "off";
}

// Conserve l'ecart entre l'horloge NTP et l'horloge locale, et le rafraichit
// periodiquement dans un thread dedie pour ne jamais bloquer la publication.
class TimeKeeper {
 public:
  TimeKeeper(std::string server, int timeout_ms, std::chrono::seconds interval)
      : server_(std::move(server)), timeout_ms_(timeout_ms), interval_(interval) {}

  ~TimeKeeper() { stop(); }

  // Effectue une premiere synchro (bloquante) puis lance la resynchronisation
  // periodique en arriere-plan.
  void start() {
    next_delay_ = sync_once() ? interval_ : std::chrono::seconds(10);
    worker_ = std::thread([this] { run(); });
  }

  // Demande l'arret du thread et attend sa fin. Idempotent.
  void stop() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_) return;
      stopping_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
  }

  // Heure courante corrigee de l'ecart NTP, en secondes Unix (UTC).
  int64_t now_unix_seconds() const {
    const int64_t local_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();
    const int64_t corrected_ns = local_ns + offset_ns_.load(std::memory_order_relaxed);
    return corrected_ns / 1000000000LL;
  }

 private:
  // Interroge le serveur NTP et met a jour l'ecart. Renvoie false en cas d'echec,
  // auquel cas l'ecart precedent est conserve.
  bool sync_once() {
    if (ntp_disabled(server_)) {
      LOG_WARN("NTP desactive (NTP_SERVER='" + server_ + "'), utilisation de l'horloge locale");
      return false;
    }

    const NtpResult result = query_ntp(server_, timeout_ms_);
    if (!result.ok) {
      LOG_WARN("Synchro NTP echouee sur " + result.server + " : " + result.error +
               " - conservation de l'ecart precedent");
      return false;
    }

    offset_ns_.store(result.offset_ns, std::memory_order_relaxed);

    char message[320];
    std::snprintf(message, sizeof(message),
                  "Synchro NTP OK serveur=%s stratum=%d ecart=%+.3f ms aller-retour=%.3f ms",
                  result.server.c_str(), result.stratum,
                  static_cast<double>(result.offset_ns) / 1e6,
                  static_cast<double>(result.delay_ns) / 1e6);
    LOG_INFO(std::string(message));
    return true;
  }

  // Boucle de resynchronisation : attend le delai prevu, puis interroge le NTP.
  void run() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stopping_) {
      if (cv_.wait_for(lock, next_delay_, [this] { return stopping_; })) break;

      lock.unlock();
      const bool synced = sync_once();
      lock.lock();

      // En cas d'echec on retente rapidement, sinon on reprend la periode normale.
      next_delay_ = synced ? interval_ : std::chrono::seconds(10);
    }
  }

  std::string server_;
  int timeout_ms_;
  std::chrono::seconds interval_;
  std::chrono::seconds next_delay_{20};

  std::atomic<int64_t> offset_ns_{0};  // ecart NTP en nanosecondes (atomique : lu chaque seconde)
  std::mutex mutex_;
  std::condition_variable cv_;
  bool stopping_ = false;
  std::thread worker_;
};

// Construit le message JSON attendu par le reste de la chaine.
std::string payload_for(int64_t unix_seconds) {
  return "{\"UnixTimeStamp\":" + std::to_string(unix_seconds) + "}";
}

}  // namespace

int main() {
  // Arret propre sur Ctrl+C / docker stop, et pas de mort sur un socket ferme.
  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);
  std::signal(SIGPIPE, SIG_IGN);

  const Config config = Config::from_env();

  LOG_INFO("Demarrage de ntp-mqtt-clock");
  LOG_INFO("MQTT " + config.mqtt_host + ":" + std::to_string(config.mqtt_port) + " topic=" +
           config.mqtt_topic + " utilisateur=" + config.mqtt_username);
  LOG_INFO("NTP " + config.ntp_server + " resynchronisation toutes les " +
           std::to_string(config.ntp_resync_minutes) + " min");
  LOG_INFO("publication toutes les " + std::to_string(config.publish_interval_ms) + " ms");

  TimeKeeper keeper(config.ntp_server, config.ntp_timeout_ms,
                    std::chrono::seconds(config.ntp_resync_minutes * 60LL));
  keeper.start();

  MqttPublisher mqtt(MqttConfig{config.mqtt_host, config.mqtt_port, config.mqtt_username,
                               config.mqtt_password, config.mqtt_topic, config.mqtt_client_id});

  uint64_t published = 0;
  bool logged_first = false;
  // Planification sur steady_clock : l'echeance ne bouge pas si l'heure systeme est corrigee.
  auto next_tick = std::chrono::steady_clock::now();

  while (g_running) {
    next_tick += std::chrono::milliseconds(config.publish_interval_ms);

    const std::string payload = payload_for(keeper.now_unix_seconds());
    if (mqtt.publish(payload)) {
      ++published;
      // On trace le premier message puis un par minute pour garder des logs lisibles.
      if (!logged_first || published % 60 == 0) {
        LOG_INFO("publication " + payload + " (" + std::to_string(published) + " messages)");
        logged_first = true;
      }
    }
    // Fait tourner la boucle reseau de libmosquitto (keepalive, reconnexion).
    mqtt.poll();

    std::this_thread::sleep_until(next_tick);

    // Si le processus a ete suspendu, on ne rattrape pas les tics manques.
    if (std::chrono::steady_clock::now() > next_tick + 2s) {
      next_tick = std::chrono::steady_clock::now();
    }
  }

  LOG_INFO("Arret apres " + std::to_string(published) + " messages publies");
  keeper.stop();
  return 0;
}
