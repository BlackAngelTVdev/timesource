# Configuration de compilation du service ntp-mqtt-clock (C++17).
cmake_minimum_required(VERSION 3.16)
project(ntp_mqtt_clock LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Release par defaut (optimisations), surchargeable avec -DCMAKE_BUILD_TYPE=Debug.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Type de compilation" FORCE)
endif()

find_package(Threads REQUIRED)

# libmosquitto : client MQTT. Sur Debian/Ubuntu c'est le paquet libmosquitto-dev.
find_path(MOSQUITTO_INCLUDE_DIR NAMES mosquitto.h)
find_library(MOSQUITTO_LIBRARY NAMES mosquitto)
if(NOT MOSQUITTO_INCLUDE_DIR OR NOT MOSQUITTO_LIBRARY)
  message(FATAL_ERROR "libmosquitto introuvable - installer libmosquitto-dev (Debian/Ubuntu) ou mosquitto-devel (Fedora)")
endif()

add_executable(ntp-mqtt-clock
  src/main.cpp
  src/mqtt_publisher.cpp
  src/ntp_client.cpp
)

target_include_directories(ntp-mqtt-clock PRIVATE ${MOSQUITTO_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ntp-mqtt-clock PRIVATE ${MOSQUITTO_LIBRARY} Threads::Threads)
# Avertissements complets : le code doit compiler sans warning.
target_compile_options(ntp-mqtt-clock PRIVATE -Wall -Wextra -Wpedantic)
