#pragma once

#include <cstdlib>
#include <string>

// Lecture d'une variable d'environnement texte ; la valeur par defaut est utilisee
// si la variable est absente ou vide.
inline std::string env_string(const char* name, const std::string& fallback) {
  const char* value = std::getenv(name);
  return (value != nullptr && *value != '\0') ? std::string(value) : fallback;
}

// Lecture d'une variable d'environnement entiere ; la valeur par defaut est utilisee
// si la variable est absente ou invalide.
inline int env_int(const char* name, int fallback) {
  const char* value = std::getenv(name);
  if (value == nullptr || *value == '\0') return fallback;
  try {
    return std::stoi(value);
  } catch (...) {
    return fallback;
  }
}

// Configuration complete du service, avec les valeurs par defaut du projet.
// Tout est surchargeable par variable d'environnement (voir README).
struct Config {
  // MQTT
  std::string mqtt_host;
  int mqtt_port = 1883;
  std::string mqtt_username;
  std::string mqtt_password;
  std::string mqtt_topic;
  std::string mqtt_client_id;

  // NTP
  std::string ntp_server;
  int ntp_resync_minutes = 20;
  int ntp_timeout_ms = 3000;

  // Publication
  int publish_interval_ms = 1000;

  static Config from_env() {
    Config config;
    config.mqtt_host = env_string("MQTT_HOST", "mqqt.blue.section-inf.ch");
    config.mqtt_port = env_int("MQTT_PORT", 1883);
    config.mqtt_username = env_string("MQTT_USERNAME", "ict");
    config.mqtt_password = env_string("MQTT_PASSWORD", "321");
    config.mqtt_topic = env_string("MQTT_TOPIC", "umddcmid3b/dg/time");
    config.mqtt_client_id = env_string("MQTT_CLIENT_ID", "umddcmid3b-dg-time");

    config.ntp_server = env_string("NTP_SERVER", "pool.ntp.org");
    config.ntp_resync_minutes = env_int("NTP_SYNC_INTERVAL_MIN", 20);
    config.ntp_timeout_ms = env_int("NTP_TIMEOUT_MS", 3000);

    config.publish_interval_ms = env_int("PUBLISH_INTERVAL_MS", 1000);

    // On garde des valeurs exploitables meme si l'environnement est mal configure.
    if (config.mqtt_port <= 0 || config.mqtt_port > 65535) config.mqtt_port = 1883;
    if (config.ntp_resync_minutes < 1) config.ntp_resync_minutes = 20;
    if (config.ntp_timeout_ms < 200) config.ntp_timeout_ms = 3000;
    if (config.publish_interval_ms < 100) config.publish_interval_ms = 1000;

    return config;
  }
};
