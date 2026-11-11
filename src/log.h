#pragma once

#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>

// Journalisation minimale et horodatee en UTC sur la sortie standard :
// ces lignes se lisent directement avec `docker logs` / `docker compose logs`.
namespace logging {

inline void line(const char* level, const std::string& message) {
  const auto now = std::chrono::system_clock::now();
  const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
  gmtime_r(&seconds, &utc);

  char stamp[32] = {0};
  std::strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", &utc);
  std::printf("%s [%s] %s\n", stamp, level, message.c_str());
  std::fflush(stdout);  // vidage immediat pour ne rien perdre en cas d'arret brutal
}

}  // namespace logging

#define LOG_INFO(message) logging::line("INFO", (message))
#define LOG_WARN(message) logging::line("AVERT", (message))
#define LOG_ERROR(message) logging::line("ERREUR", (message))
