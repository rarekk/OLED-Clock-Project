# OLED Clock - convenience wrapper around the Docker toolchain.
#
# Examples:
#   make build
#   make menuconfig
#   make flash PORT=/dev/ttyUSB0
#   make flash PORT=/dev/ttyUSB0 MONITOR=1
#   make shell
#
# On Linux/macOS pass the serial device; on Windows use the PowerShell script
# in scripts/clock.ps1 instead (see README.md).

COMPOSE     := docker compose -f docker/docker-compose.yml
COMPOSE_FL  := $(COMPOSE) -f docker/docker-compose.flash.yml
IDF         := $(COMPOSE) run --rm idf
IDF_FLASH   := $(COMPOSE_FL) run --rm idf

PORT        ?= /dev/ttyUSB0

.PHONY: help build menuconfig flash monitor flash-monitor erase clean fullclean shell

help:
	@echo "Targets: build menuconfig flash monitor flash-monitor erase clean fullclean shell"
	@echo "Vars:    PORT=/dev/ttyUSB0  MONITOR=1"

build:
	$(IDF) idf.py build

menuconfig:
	$(IDF) idf.py menuconfig

flash:
	$(IDF_FLASH) idf.py -p $(PORT) flash $(if $(MONITOR),monitor,)

monitor:
	$(IDF_FLASH) idf.py -p $(PORT) monitor

flash-monitor:
	$(IDF_FLASH) idf.py -p $(PORT) flash monitor

erase:
	$(IDF_FLASH) idf.py -p $(PORT) erase-flash

clean:
	$(IDF) idf.py clean

fullclean:
	$(IDF) idf.py fullclean

shell:
	$(COMPOSE) run --rm idf bash
