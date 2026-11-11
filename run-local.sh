#!/usr/bin/env bash
# Compile et lance l'horloge NTP -> MQTT en local, sans Docker.
# Charge les variables du fichier .env (identifiants et topic MQTT), puis demarre le binaire.
set -euo pipefail

cd "$(dirname "$0")"

missing=0
for tool in cmake g++; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Outil manquant : $tool" >&2
    missing=1
  fi
done
if [ "$missing" -ne 0 ]; then
  exit 1
fi

if ! printf '#include <mosquitto.h>\nint main() { return 0; }\n' \
     | g++ -x c++ - -o /dev/null -lmosquitto 2>/dev/null; then
  cat >&2 <<'EOF'
libmosquitto (en-tetes + bibliotheque) introuvable.
  Debian/Ubuntu : sudo apt install build-essential cmake libmosquitto-dev
  Fedora        : sudo dnf install gcc-c++ cmake mosquitto-devel
  Arch          : sudo pacman -S base-devel cmake mosquitto
  macOS         : brew install cmake mosquitto
EOF
  exit 1
fi

# Charge .env ligne par ligne (COMMENTAIRE / LIGNE_VIDE ignorees, quotes retirees).
if [ -f .env ]; then
  echo "== chargement de .env =="
  while IFS= read -r line || [ -n "$line" ]; do
    case "$line" in
      '' | '#'*) continue ;;
    esac
    key=${line%%=*}
    value=${line#*=}
    value=${value%\"}
    value=${value#\"}
    value=${value%\'}
    value=${value#\'}
    export "$key=$value"
  done < .env
else
  echo "== pas de .env, utilisation des valeurs par defaut =="
fi

echo "== compilation =="
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build --parallel "$(nproc 2>/dev/null || echo 2)"

echo "== lancement (Ctrl+C pour arreter) =="
exec ./build/ntp-mqtt-clock
