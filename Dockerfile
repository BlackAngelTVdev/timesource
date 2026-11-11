# syntax=docker/dockerfile:1
#
# Image du service ntp-mqtt-clock.
# Construction en deux etapes : une image de build avec les outils et les en-tetes,
# puis une image d'execution minimale qui ne contient que le binaire et la bibliotheque.
# Aucun port a publier : le service est uniquement client (MQTT TCP/1883, NTP UDP/123).

# ---------- etape 1 : compilation ----------
FROM debian:bookworm-slim AS build

# Outils de compilation + en-tetes de libmosquitto.
RUN apt-get update \
 && apt-get install -y --no-install-recommends build-essential cmake libmosquitto-dev \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY src/ ./src/

# Compilation en Release, avec autant de coeurs que disponibles.
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build build --parallel "$(nproc)"

# ---------- etape 2 : execution ----------
FROM debian:bookworm-slim AS runtime

# Seule la bibliotheque partagee est necessaire au binaire compile.
RUN apt-get update \
 && apt-get install -y --no-install-recommends libmosquitto1 \
 && rm -rf /var/lib/apt/lists/* \
 && useradd --system --uid 10001 --no-create-home app

COPY --from=build /src/build/ntp-mqtt-clock /usr/local/bin/ntp-mqtt-clock

# Execution avec un utilisateur non privilegie.
USER app
ENTRYPOINT ["/usr/local/bin/ntp-mqtt-clock"]
