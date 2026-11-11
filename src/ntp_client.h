#pragma once

#include <cstdint>
#include <string>

// Resultat d'une requete SNTP.
struct NtpResult {
  bool ok = false;
  int64_t offset_ns = 0;  // heure du serveur moins heure locale
  int64_t delay_ns = 0;   // temps aller-retour estime
  int stratum = 0;        // distance a la source de reference (1 = meilleur)
  std::string server;     // adresse reellement interrogee, pour les logs
  std::string error;      // raison de l'echec si ok == false
};

// Envoie une requete SNTP (UDP/123), attend la reponse et calcule l'ecart d'horloge.
// Bloquant : a appeler hors de la boucle de publication.
NtpResult query_ntp(const std::string& server, int timeout_ms);
