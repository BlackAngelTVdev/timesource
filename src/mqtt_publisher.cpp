// Implementation du publisher MQTT : connexion authentifiee, publication QoS 0
// et reconnexion automatique tant que le processus tourne.
// Les messages de libmosquitto (en anglais) sont traduits pour les logs.

#include "mqtt_publisher.h"

#include <mosquitto.h>

#include <string>
#include <utility>

#include "log.h"

namespace {

constexpr auto kReconnectDelay = std::chrono::seconds(5);
constexpr auto kConnectTimeout = std::chrono::seconds(5);

std::string target(const MqttConfig& config) {
  return config.host + ":" + std::to_string(config.port);
}

// Traduction francaise des codes d'erreur de libmosquitto.
std::string mqtt_error_fr(int rc) {
  switch (rc) {
    case MOSQ_ERR_SUCCESS:
      return "succes";
    case MOSQ_ERR_NOMEM:
      return "memoire insuffisante";
    case MOSQ_ERR_PROTOCOL:
      return "erreur de protocole MQTT";
    case MOSQ_ERR_INVAL:
      return "parametre invalide";
    case MOSQ_ERR_NO_CONN:
      return "non connecte au broker";
    case MOSQ_ERR_CONN_REFUSED:
      return "connexion refusee par le broker";
    case MOSQ_ERR_NOT_FOUND:
      return "element introuvable";
    case MOSQ_ERR_CONN_LOST:
      return "connexion perdue";
    case MOSQ_ERR_TLS:
      return "erreur TLS";
    case MOSQ_ERR_PAYLOAD_SIZE:
      return "message trop volumineux";
    case MOSQ_ERR_NOT_SUPPORTED:
      return "operation non supportee";
    case MOSQ_ERR_AUTH:
      return "authentification refusee";
    case MOSQ_ERR_ACL_DENIED:
      return "acces refuse par les ACL du broker";
    case MOSQ_ERR_UNKNOWN:
      return "erreur inconnue";
    case MOSQ_ERR_ERRNO:
      return "erreur systeme";
    default:
      return std::string("erreur MQTT ") + mosquitto_strerror(rc);
  }
}

// Traduction francaise des codes de retour du CONNACK.
std::string connack_fr(int rc) {
  switch (rc) {
    case 0:
      return "connexion acceptee";
    case 1:
      return "version de protocole refusee";
    case 2:
      return "identifiant de client refuse";
    case 3:
      return "serveur MQTT indisponible";
    case 4:
      return "identifiants invalides (utilisateur ou mot de passe)";
    case 5:
      return "connexion non autorisee";
    default:
      return "code de retour inconnu " + std::to_string(rc);
  }
}

}  // namespace

MqttPublisher::MqttPublisher(MqttConfig config) : config_(std::move(config)) {
  mosquitto_lib_init();

  // Session propre : le broker ne conserve rien entre deux demarrages.
  mosq_ = mosquitto_new(config_.client_id.c_str(), true, this);
  if (mosq_ == nullptr) {
    LOG_ERROR("allocation du client MQTT impossible");
    return;
  }

  mosquitto_username_pw_set(
      mosq_, config_.username.empty() ? nullptr : config_.username.c_str(),
      config_.password.empty() ? nullptr : config_.password.c_str());
  mosquitto_connect_callback_set(mosq_, &MqttPublisher::on_connect);
  mosquitto_disconnect_callback_set(mosq_, &MqttPublisher::on_disconnect);
  mosquitto_log_callback_set(mosq_, &MqttPublisher::on_log);
  mosquitto_reconnect_delay_set(mosq_, 1, 30, false);
}

MqttPublisher::~MqttPublisher() {
  if (mosq_ != nullptr) {
    mosquitto_disconnect(mosq_);
    mosquitto_destroy(mosq_);
    mosq_ = nullptr;
  }
  mosquitto_lib_cleanup();
}

bool MqttPublisher::publish(const std::string& payload) {
  if (mosq_ == nullptr) return false;

  if (!connected_.load()) {
    if (!try_connect()) return false;
  }

  // QoS 0 : pas d'accuse de reception, le message est envoye "au mieux".
  const int rc = mosquitto_publish(mosq_, nullptr, config_.topic.c_str(),
                                   static_cast<int>(payload.size()), payload.data(), 0, false);
  if (rc != MOSQ_ERR_SUCCESS) {
    LOG_WARN("publication MQTT echouee : " + mqtt_error_fr(rc));
    if (rc == MOSQ_ERR_NO_CONN || rc == MOSQ_ERR_CONN_LOST) connected_.store(false);
    return false;
  }
  return true;
}

void MqttPublisher::poll() {
  if (mosq_ == nullptr) return;

  // timeout 0 : simple traitement des evenements reseau sans bloquer.
  const int rc = mosquitto_loop(mosq_, 0, 1);
  if (rc != MOSQ_ERR_SUCCESS && rc != MOSQ_ERR_NO_CONN) {
    LOG_WARN("erreur de la boucle reseau MQTT : " + mqtt_error_fr(rc));
    connected_.store(false);
  }
}

bool MqttPublisher::try_connect() {
  // Anti-rebond : inutile de retenter a chaque seconde si le broker est injoignable.
  const auto now = std::chrono::steady_clock::now();
  if (now < next_attempt_) return false;
  next_attempt_ = now + kReconnectDelay;

  // Ferme un eventuel socket laisse ouvert par une tentative precedente.
  if (mosquitto_socket(mosq_) != -1) {
    mosquitto_disconnect(mosq_);
  }

  LOG_INFO("connexion MQTT a " + target(config_) + " avec l'utilisateur '" + config_.username +
           "'");
  int rc = mosquitto_connect_async(mosq_, config_.host.c_str(), config_.port,
                                   config_.keepalive_seconds);
  if (rc != MOSQ_ERR_SUCCESS) {
    LOG_WARN("connexion MQTT impossible : " + mqtt_error_fr(rc));
    return false;
  }

  // Fait avancer la boucle reseau jusqu'au CONNACK, une erreur, ou le timeout.
  const auto deadline = std::chrono::steady_clock::now() + kConnectTimeout;
  while (std::chrono::steady_clock::now() < deadline) {
    rc = mosquitto_loop(mosq_, 100, 1);
    if (connected_.load()) {
      // Connexion etablie : on autorise une reconnexion immediate si elle est perdue.
      next_attempt_ = std::chrono::steady_clock::now();
      return true;
    }
    if (rc != MOSQ_ERR_SUCCESS && rc != MOSQ_ERR_NO_CONN) {
      LOG_WARN("connexion MQTT a " + target(config_) + " echouee : " + mqtt_error_fr(rc));
      break;
    }
  }

  if (!connected_.load()) {
    LOG_WARN("connexion MQTT a " + target(config_) + " echouee, nouvel essai dans 5 s");
  }
  connected_.store(false);
  return connected_.load();
}

void MqttPublisher::on_connect(struct mosquitto*, void* userdata, int rc) {
  auto* self = static_cast<MqttPublisher*>(userdata);
  if (rc == 0) {
    self->connected_.store(true);
    LOG_INFO("MQTT connecte a " + target(self->config_));
  } else {
    self->connected_.store(false);
    LOG_WARN("connexion MQTT refusee : " + connack_fr(rc));
  }
}

void MqttPublisher::on_disconnect(struct mosquitto*, void* userdata, int rc) {
  auto* self = static_cast<MqttPublisher*>(userdata);
  self->connected_.store(false);
  if (rc != 0) {
    LOG_WARN("connexion MQTT perdue : " + mqtt_error_fr(rc));
  }
}

// On ne remonte que les avertissements et erreurs de la bibliotheque.
void MqttPublisher::on_log(struct mosquitto*, void*, int level, const char* message) {
  if (level == MOSQ_LOG_WARNING || level == MOSQ_LOG_ERR) {
    LOG_WARN(std::string("mosquitto : ") + message);
  }
}
