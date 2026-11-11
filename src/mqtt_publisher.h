#pragma once

#include <atomic>
#include <chrono>
#include <string>

// Type opaque de libmosquitto (evite d'inclure mosquitto.h dans tous les fichiers).
struct mosquitto;

// Parametres de connexion au broker.
struct MqttConfig {
  std::string host;
  int port = 1883;
  std::string username;
  std::string password;
  std::string topic;
  std::string client_id;
  int keepalive_seconds = 30;
};

// Petite surcouche de libmosquitto : publication en QoS 0 et reconnexion automatique.
class MqttPublisher {
 public:
  explicit MqttPublisher(MqttConfig config);
  ~MqttPublisher();

  MqttPublisher(const MqttPublisher&) = delete;
  MqttPublisher& operator=(const MqttPublisher&) = delete;

  // Publie le message sur le topic configure ; se connecte d'abord si necessaire.
  bool publish(const std::string& payload);

  // Fait tourner la boucle reseau de libmosquitto (keepalive) ; a appeler regulierement.
  void poll();

  bool connected() const { return connected_.load(); }

 private:
  // Tentative de connexion (avec anti-rebond) ; renvoie true si le CONNACK est arrive.
  bool try_connect();

  // Callbacks statiques de libmosquitto : l'instance est retrouvee via userdata.
  static void on_connect(struct mosquitto* mosq, void* userdata, int rc);
  static void on_disconnect(struct mosquitto* mosq, void* userdata, int rc);
  static void on_log(struct mosquitto* mosq, void* userdata, int level, const char* message);

  MqttConfig config_;
  struct mosquitto* mosq_ = nullptr;
  std::atomic<bool> connected_{false};
  std::chrono::steady_clock::time_point next_attempt_{};
};
