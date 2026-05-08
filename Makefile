SHELL := /bin/bash

.DEFAULT_GOAL := help

RELEASE_VERSION ?= v2.1.2

HEZI_BOARD := zhengchen-1.54tft-wifi
P4_BOARD := waveshare-p4-86-panel-eth-2ro
SS_BOARD := houzzkit-smart-speaker

HEZI_ZIP := releases/$(RELEASE_VERSION)/$(RELEASE_VERSION)_$(HEZI_BOARD).zip
P4_ZIP := releases/$(RELEASE_VERSION)/$(RELEASE_VERSION)_$(P4_BOARD).zip
SS_ZIP := releases/$(RELEASE_VERSION)/$(RELEASE_VERSION)_$(SS_BOARD).zip

HEZI_TMP ?= /tmp/p3-hezi
P4_TMP ?= /tmp/p4-86flash
SS_TMP ?= /tmp/houzzkit-smart-speaker

HEZI_PORT ?= /dev/cu.wchusbserial110
P4_PORT ?= /dev/cu.usbmodem5AB01652131
SS_PORT ?= /dev/cu.usbmodem1101

HEZI_BAUD ?= 1500000
P4_BAUD ?= 460800
SS_BAUD ?= 1500000

BACKUP_ROOT ?= ..
IDF_EXPORT ?= /Users/resmo/esp/esp-idf/export.sh
IDF_SETUP = if [ -f "$(IDF_EXPORT)" ]; then . "$(IDF_EXPORT)" >/dev/null 2>&1; fi
BOARD_GOALS := $(filter hezi p4 ss,$(MAKECMDGOALS))

.PHONY: help clear build flash hezi p4 ss build-hezi build-p4 build-ss flash-hezi flash-p4 flash-ss

help:
	@printf '%s\n' \
		'可用命令:' \
		'  make clear       备份并清理本地 ignored 构建输入' \
		'  make build hezi  清理 build/ 后打 zhengchen-1.54tft-wifi 包' \
		'  make build p4    清理 build/ 后打 waveshare-p4-86-panel-eth-2ro 包' \
		'  make build ss    清理 build/ 后打 houzzkit-smart-speaker 包' \
		'  make flash hezi  烧录 hezi release zip 里的 merged-binary.bin' \
		'  make flash p4    烧录 p4 release zip 里的 merged-binary.bin' \
		'  make flash ss    烧录 ss release zip 里的 merged-binary.bin'

clear:
	@set -euo pipefail; \
	ts=$$(date +%Y%m%d-%H%M%S); \
	backup="$(BACKUP_ROOT)/houzzkit-ai-build-input-backup-$$ts"; \
	mkdir -p "$$backup"; \
	[ -d managed_components ] && cp -a managed_components "$$backup/" || true; \
	[ -f dependencies.lock ] && cp -a dependencies.lock "$$backup/" || true; \
	[ -f sdkconfig ] && cp -a sdkconfig "$$backup/" || true; \
	[ -f sdkconfig.old ] && cp -a sdkconfig.old "$$backup/" || true; \
	rm -rf managed_components dependencies.lock sdkconfig sdkconfig.old build; \
	printf '本地构建输入已清理，备份目录: %s\n' "$$backup"

build:
	@case "$(BOARD_GOALS)" in \
		hezi) $(MAKE) --no-print-directory build-hezi ;; \
		p4) $(MAKE) --no-print-directory build-p4 ;; \
		ss) $(MAKE) --no-print-directory build-ss ;; \
		*) printf '用法: make build hezi、make build p4 或 make build ss\n' >&2; exit 2 ;; \
	esac

flash:
	@case "$(BOARD_GOALS)" in \
		hezi) $(MAKE) --no-print-directory flash-hezi ;; \
		p4) $(MAKE) --no-print-directory flash-p4 ;; \
		ss) $(MAKE) --no-print-directory flash-ss ;; \
		*) printf '用法: make flash hezi、make flash p4 或 make flash ss\n' >&2; exit 2 ;; \
	esac

hezi p4 ss:
	@:

build-hezi:
	rm -rf build
	$(IDF_SETUP); idf.py set-target esp32s3
	$(IDF_SETUP); python3 scripts/release.py $(HEZI_BOARD) --name $(HEZI_BOARD)

build-p4:
	rm -rf build
	# $(IDF_SETUP); idf.py set-target esp32p4
	$(IDF_SETUP); python3 scripts/release.py $(P4_BOARD) --name $(P4_BOARD)

build-ss:
	rm -rf build
	$(IDF_SETUP); python3 scripts/release.py $(SS_BOARD) --name $(SS_BOARD)

flash-hezi:
	rm -rf $(HEZI_TMP)
	unzip -o $(HEZI_ZIP) -d $(HEZI_TMP)
	python3 -m esptool --chip esp32s3 -p $(HEZI_PORT) -b $(HEZI_BAUD) --before default_reset --after hard_reset write_flash 0x0 $(HEZI_TMP)/merged-binary.bin

flash-p4:
	rm -rf $(P4_TMP)
	unzip -o $(P4_ZIP) -d $(P4_TMP)
	python3 -m esptool --chip esp32p4 -p $(P4_PORT) -b $(P4_BAUD) write_flash 0x0 $(P4_TMP)/merged-binary.bin

flash-ss:
	rm -rf $(SS_TMP)
	unzip -o $(SS_ZIP) -d $(SS_TMP)
	python3 -m esptool --chip esp32s3 -p $(SS_PORT) -b $(SS_BAUD) --before default_reset --after hard_reset write_flash 0x0 $(SS_TMP)/merged-binary.bin
