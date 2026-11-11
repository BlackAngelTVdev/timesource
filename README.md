# ntp-mqtt-clock

Service C++ qui publie l'heure courante en JSON sur un broker MQTT, une fois par seconde,
en se resynchronisant sur un serveur NTP de reference toutes les 20 minutes.

Depot : <https://github.com/BlackAngelTVdev/timesource>
Image : `ghcr.io/blackangeltvdev/timesource:latest`

```json
{"UnixTimeStamp":1794218169}
```

## Fonctionnement

1. Au demarrage, une requete SNTP (UDP/123) est envoyee au serveur NTP et l'ecart avec l'horloge
   locale est memorise.
2. Une tache de fond renouvelle cet ecart toutes les 20 minutes (et reessaie toutes les 10 s si le
   serveur NTP ne repond pas).
3. La boucle principale publie chaque seconde `{"UnixTimeStamp":<secondes>}` sur le topic MQTT,
   avec reconnexion automatique au broker (QoS 0).
4. Tous les messages (y compris les erreurs) sont en francais et horodates en UTC sur la sortie
   standard.

## Structure du code

| Fichier | Contenu |
| --- | --- |
| `src/main.cpp` | Boucle principale, `TimeKeeper` (synchro NTP periodique), arret propre |
| `src/ntp_client.{h,cpp}` | Client SNTP en UDP/123, calcul de l'ecart d'horloge |
| `src/mqtt_publisher.{h,cpp}` | Publisher libmosquitto, reconnexion, messages d'erreur en francais |
| `src/config.h` | Configuration et valeurs par defaut (variables d'environnement) |
| `src/log.h` | Journalisation horodatee (`INFO`, `AVERT`, `ERREUR`) |
| `CMakeLists.txt` | Build C++17 |
| `.env` | Identifiants, topic MQTT et serveur NTP (non versionne) |
| `.env.example` | Modele de configuration versionne, a copier en `.env` |
| `run-local.sh` | Compilation + lancement en local, sans Docker |
| `Dockerfile` | Image multi-etapes, execution non privilegiee |
| `docker-compose.yml` | Service pret, alimente par `.env` |
| `.github/workflows/image.yml` | CI : construit et publie l'image sur ghcr.io |
| `README.md` | Cette documentation |

Le code source est commente en francais.

## Configuration : fichier `.env`

Le fichier `.env` a la racine contient les identifiants et le topic. C'est le seul endroit a
modifier pour changer d'utilisateur, de mot de passe ou de topic :

```ini
MQTT_HOST=mqqt.blue.section-inf.ch
MQTT_PORT=1883
MQTT_USERNAME=ict
MQTT_PASSWORD=321
MQTT_TOPIC=umddcmid3b/dg/time
MQTT_CLIENT_ID=umddcmid3b-dg-time

NTP_SERVER=pool.ntp.org
NTP_SYNC_INTERVAL_MIN=20
NTP_TIMEOUT_MS=3000
PUBLISH_INTERVAL_MS=1000
```

Pour partir du modele versionne :

```bash
cp .env.example .env
```

`.env.example` contient exactement les memes cles, renseignees avec les valeurs par defaut du
code ; seul `.env` est ignore par Git.

- `docker compose` lit `.env` automatiquement.
- `./run-local.sh` charge `.env` au demarrage (commentaires et lignes vides ignores).
- `.env` est exclu de Git (`.gitignore`) et du contexte Docker (`.dockerignore`).
- Les valeurs ecrites dans `.env` ecrasent celles du shell pour `run-local.sh` ; pour un essai
  ponctuel sans toucher au fichier, lancer directement le binaire avec la variable voulue :
  `MQTT_TOPIC=test/topic ./build/ntp-mqtt-clock`.

Les valeurs de repli (si `.env` est absent) sont dans `src/config.h` :

| Variable | Defaut | Role |
| --- | --- | --- |
| `MQTT_HOST` | `mqqt.blue.section-inf.ch` | Broker MQTT |
| `MQTT_PORT` | `1883` | Port TCP du broker |
| `MQTT_USERNAME` | `ict` | Utilisateur MQTT |
| `MQTT_PASSWORD` | `321` | Mot de passe MQTT |
| `MQTT_TOPIC` | `umddcmid3b/dg/time` | Topic de publication |
| `MQTT_CLIENT_ID` | `umddcmid3b-dg-time` | Client ID (un seul publisher a la fois) |
| `NTP_SERVER` | `pool.ntp.org` | Serveur NTP (`none` pour utiliser l'horloge locale) |
| `NTP_SYNC_INTERVAL_MIN` | `20` | Periode de resynchronisation (minutes) |
| `NTP_TIMEOUT_MS` | `3000` | Timeout d'une requete NTP |
| `PUBLISH_INTERVAL_MS` | `1000` | Periode de publication |

## Lancement local (sans Docker)

```bash
./run-local.sh          # compile puis demarre
```

Le script verifie `cmake`, `g++` et `libmosquitto`, affiche la commande d'installation adaptee si
besoin, charge `.env`, puis lance le binaire. Manuellement :

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
set -a; . ./.env; set +a
./build/ntp-mqtt-clock
```

## Lancement avec Docker

```bash
docker compose up --build -d     # demarre en arriere-plan
docker compose logs -f           # suit les logs
docker compose down              # arrete
```

Ou sans compose :

```bash
docker build -t ntp-mqtt-clock .
docker run -d --name ntp-mqtt-clock --restart unless-stopped --env-file .env ntp-mqtt-clock
```

L'image est construite en deux etapes (compilation puis runtime `debian:bookworm-slim`) et tourne
avec un utilisateur non privilegie. Aucun port a exposer : le programme est uniquement client
(MQTT sortant TCP/1883, NTP sortant UDP/123).

## Lancement avec Podman

Le `Dockerfile` et `docker-compose.yml` fonctionnent tels quels avec Podman (image construite et
service lance avec `podman` rootless, sans option supplementaire) :

```bash
podman build -t ntp-mqtt-clock:latest .
podman run -d --name ntp-mqtt-clock --restart=unless-stopped --env-file .env ntp-mqtt-clock:latest
podman logs -f ntp-mqtt-clock                 # suit les logs
podman stop ntp-mqtt-clock && podman rm ntp-mqtt-clock
```

Avec `podman-compose` (ou `podman compose`), les memes commandes que Docker :

```bash
podman-compose up -d --build
podman-compose logs -f
podman-compose down
```

Comme le conteneur n'ecrit sur aucun volume, n'ouvre aucun port et n'utilise que le reseau
sortant, il n'y a ni option `:Z` ni `--network` a ajouter. Si `podman-compose` n'est pas
installe : `pip install --user podman-compose` (ou `podman compose` avec le paquet
`docker-compose`).

## Image preconstruite

L'image est construite et publiee sur GitHub Container Registry a chaque push sur `main`
(voir `.github/workflows/image.yml`) :

```bash
podman pull ghcr.io/blackangeltvdev/timesource:latest
podman run -d --name ntp-mqtt-clock --restart=unless-stopped --env-file .env \
  ghcr.io/blackangeltvdev/timesource:latest
docker pull ghcr.io/blackangeltvdev/timesource:latest   # equivalent avec Docker
```

Etiquettes disponibles : `latest` et le sha complet du commit. Le package est public (il est
rattache au depot public par la CI), donc aucune authentification n'est necessaire pour le tirer.

## Verifier ce qui arrive sur le broker

```bash
mosquitto_sub -h mqqt.blue.section-inf.ch -u ict -P 321 -t "umddcmid3b/dg/time" -v
```

## Depannage

- **`Synchro NTP echouee ... delai depasse`** : l'UDP/123 est bloque (pare-feu ou reseau
  d'entreprise). L'heure systeme est alors utilisee ; on peut pointer `NTP_SERVER` vers un serveur
  NTP interne.
- **`connexion MQTT refusee : identifiants invalides`** : verifier `MQTT_USERNAME` /
  `MQTT_PASSWORD` dans `.env` (le broker a bien repondu, donc le reseau est bon).
- **`connexion MQTT ... echouee`** : verifier `MQTT_HOST`, `MQTT_PORT` et la joignabilite du port
  (`mosquitto_pub -h ... -t test -m test`).
- **Deux instances en meme temps** : elles partagent le meme `MQTT_CLIENT_ID` et se deconnectent
  mutuellement ; donner un `MQTT_CLIENT_ID` different a la seconde.
- **Les logs affichent 1 message par minute** : c'est voulu, la premiere publication puis une sur 60
  sont tracees pour garder des logs lisibles ; les messages continuent d'etre envoyes chaque seconde.

## Notes

- L'heure publiee est celle du serveur NTP (heure locale corrigee de l'ecart NTP), pas l'heure
  systeme brute.
- Seule la seconde est publiee : `UnixTimeStamp` est un entier (epoch Unix, UTC).
- Si le broker tombe, le programme continue de tourner et se reconnecte toutes les 5 s.
