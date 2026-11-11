// Client SNTP minimal (paquet de 48 octets en UDP/123) : pas de dependance externe,
// on ne recupere que l'heure du serveur et l'ecart avec l'horloge locale.
// Les erreurs systeme (strerror) sont traduites en francais pour les logs.

#include "ntp_client.h"

#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>

namespace {

constexpr int kNtpPacketSize = 48;
// Le temps NTP compte les secondes depuis le 01/01/1900, l'epoch Unix depuis 1970.
constexpr int64_t kNtpEpochOffsetSeconds = 2208988800LL;

int64_t now_unix_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

// Traduction francaise des erreurs systeme les plus courantes sur un socket UDP.
std::string errno_fr(int code) {
  switch (code) {
    case ECONNREFUSED:
      return "connexion refusee";
    case ETIMEDOUT:
      return "delai depasse";
    case EHOSTUNREACH:
      return "hote injoignable";
    case ENETUNREACH:
      return "reseau injoignable";
    case EADDRNOTAVAIL:
      return "adresse non disponible";
    case EACCES:
      return "acces refuse";
    case EINTR:
      return "appel interrompu";
    case EAGAIN:
      return "delai depasse";
    case EMSGSIZE:
      return "paquet trop grand";
    default:
      return "code d'erreur systeme " + std::to_string(code);
  }
}

// Les entiers du protocole NTP sont en gros-boutiste (big endian).
uint32_t read_be32(const uint8_t* data) {
  return (uint32_t(data[0]) << 24) | (uint32_t(data[1]) << 16) | (uint32_t(data[2]) << 8) |
         uint32_t(data[3]);
}

uint64_t read_be64(const uint8_t* data) {
  return (uint64_t(read_be32(data)) << 32) | uint64_t(read_be32(data + 4));
}

// Horodatage NTP 64 bits : 32 bits de secondes + 32 bits de fraction de seconde.
uint64_t unix_ns_to_ntp(int64_t unix_ns) {
  const int64_t seconds = unix_ns / 1000000000LL;
  const int64_t nanos = unix_ns % 1000000000LL;
  const uint64_t ntp_seconds = uint64_t(seconds + kNtpEpochOffsetSeconds) & 0xFFFFFFFFULL;
  const uint64_t fraction = (uint64_t(nanos) << 32) / 1000000000ULL;
  return (ntp_seconds << 32) | fraction;
}

int64_t ntp_to_unix_ns(const uint8_t* data) {
  int64_t unix_seconds = int64_t(read_be32(data)) - kNtpEpochOffsetSeconds;
  // Les secondes NTP sont sur 32 bits : elles repassent par zero en 2036 (ere 1).
  if (unix_seconds < 0) unix_seconds += (int64_t(1) << 32);
  const uint64_t fraction = uint64_t(read_be32(data + 4));
  const int64_t nanos = int64_t((fraction * 1000000000ULL) >> 32);
  return unix_seconds * 1000000000LL + nanos;
}

}  // namespace

NtpResult query_ntp(const std::string& server, int timeout_ms) {
  NtpResult result;
  result.server = server;

  // Resolution du nom : on accepte IPv4 et IPv6 et on essaie chaque adresse.
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;

  addrinfo* addresses = nullptr;
  if (getaddrinfo(server.c_str(), "123", &hints, &addresses) != 0) {
    result.error = "resolution impossible de '" + server + "'";
    return result;
  }

  // LI = 0 (pas d'alerte), VN = 4 (version 4), Mode = 3 (client).
  uint8_t request[kNtpPacketSize] = {};
  request[0] = 0x23;

  // t1 : heure locale juste avant l'envoi, recopiee dans le champ "transmit".
  const int64_t t1 = now_unix_ns();
  const uint64_t transmit = unix_ns_to_ntp(t1);
  for (int i = 0; i < 8; ++i) {
    request[40 + i] = uint8_t((transmit >> (56 - 8 * i)) & 0xFF);
  }

  std::string last_error = "aucune reponse du serveur NTP";
  timeval timeout{};
  timeout.tv_sec = timeout_ms / 1000;
  timeout.tv_usec = (timeout_ms % 1000) * 1000;

  for (addrinfo* address = addresses; address != nullptr && !result.ok;
       address = address->ai_next) {
    const int fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
    if (fd < 0) {
      last_error = "socket : " + errno_fr(errno);
      continue;
    }
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    if (sendto(fd, request, sizeof(request), 0, address->ai_addr, address->ai_addrlen) !=
        static_cast<ssize_t>(sizeof(request))) {
      last_error = "envoi : " + errno_fr(errno);
      close(fd);
      continue;
    }

    uint8_t response[512] = {};
    const ssize_t received = recv(fd, response, sizeof(response), 0);
    // t4 : heure locale a la reception, utilisee dans le calcul de l'ecart.
    const int64_t t4 = now_unix_ns();
    close(fd);

    if (received < kNtpPacketSize) {
      last_error = (received < 0) ? "reception : " + errno_fr(errno)
                                  : "reponse NTP trop courte";
      continue;
    }

    // Controles de base : c'est bien un serveur qui repond, et il n'est pas en erreur.
    const uint8_t mode = response[0] & 0x07;
    const int stratum = response[1];
    if (mode != 4) {
      last_error = "mode NTP inattendu " + std::to_string(mode);
      continue;
    }
    if (stratum == 0) {
      last_error = "le serveur a repondu Kiss-o'-Death (requete refusee)";
      continue;
    }
    if (stratum > 15) {
      last_error = "stratum invalide " + std::to_string(stratum);
      continue;
    }

    // Le serveur doit renvoyer notre horodatage "transmit" en champ "origine".
    const uint64_t origin = read_be64(response + 24);
    if (origin != 0 && origin != transmit) {
      last_error = "horodatage d'origine incoherent";
      continue;
    }

    const int64_t t2 = ntp_to_unix_ns(response + 32);  // reception chez le serveur
    const int64_t t3 = ntp_to_unix_ns(response + 40);  // envoi par le serveur
    if (t2 == 0 || t3 == 0) {
      last_error = "le serveur a renvoye un horodatage vide";
      continue;
    }

    // Formules SNTP : ecart moyen et aller-retour, en tenant compte du temps de transit.
    result.offset_ns = ((t2 - t1) + (t3 - t4)) / 2;
    result.delay_ns = (t4 - t1) - (t3 - t2);
    result.stratum = stratum;
    result.ok = true;
    result.error.clear();

    // Adresse effectivement joignable, utile dans les logs.
    char numeric_host[NI_MAXHOST] = {0};
    if (getnameinfo(address->ai_addr, address->ai_addrlen, numeric_host, sizeof(numeric_host),
                    nullptr, 0, NI_NUMERICHOST) == 0) {
      result.server = server + " (" + numeric_host + ")";
    }
  }

  freeaddrinfo(addresses);

  if (!result.ok) result.error = last_error;
  return result;
}
