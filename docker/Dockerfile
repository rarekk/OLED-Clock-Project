# syntax=docker/dockerfile:1
#
# Build environment for the ESP32 OLED clock firmware.
#
#   docker build -f docker/Dockerfile -t oled-clock-idf .
#
# The official Espressif image already ships the ESP-IDF toolchain and enters
# the shell with the environment exported, so no further setup is required.
ARG IDF_VERSION=release-v5.3
FROM espressif/idf:${IDF_VERSION}

LABEL org.opencontainers.image.title="oled-clock-build" \
      org.opencontainers.image.description="ESP-IDF build container for the ESP32 OLED clock project" \
      org.opencontainers.image.source="https://github.com/"

# ccache dramatically speeds up repeat builds.
ENV IDF_CCACHE_ENABLE=1

WORKDIR /project
