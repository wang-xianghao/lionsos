# Makefile for webserver.
#
# Copyright 2023, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
# This makefile will be copied into the Build directory and used from there.
#
BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
SDDF := $(LIONSOS)/dep/sddf

ifeq (${MICROKIT_BOARD},odroidc4)
	TIMER_DRIVER_DIR := meson
	ETHERNET_DRIVER_DIR := meson
	UART_DRIVER_DIR := meson
	CPU := cortex-a55
else ifeq (${MICROKIT_BOARD},qemu_virt_aarch64)
	TIMER_DRIVER_DIR := arm
	ETHERNET_DRIVER_DIR := virtio
	UART_DRIVER_DIR := arm
	CPU := cortex-a53
	QEMU := qemu-system-aarch64
else
$(error Unsupported MICROKIT_BOARD given)
endif

TOOLCHAIN := clang
CC := clang
LD := ld.lld
AR := llvm-ar
RANLIB := llvm-ranlib
TARGET := aarch64-none-elf
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit

NFS=$(LIONSOS)/components/fs/nfs
MUSL_SRC := $(LIONSOS)/dep/musllibc
MUSL := musllibc
# MICRODOT := ${LIONSOS}/dep/microdot/src

IMAGES := backend.elf \
	eth_driver.elf network_virt_rx.elf network_virt_tx.elf
# IMAGES := timer_driver.elf eth_driver.elf micropython.elf nfs.elf \
# 	  copy.elf network_virt_rx.elf network_virt_tx.elf \
# 	  uart_driver.elf serial_virt_tx.elf

SYSTEM_FILE := $(WEBSERVER_SRC_DIR)/board/$(MICROKIT_BOARD)/capp.system
# SYSTEM_FILE := $(WEBSERVER_SRC_DIR)/board/$(MICROKIT_BOARD)/webserver.system

CFLAGS := \
	-mtune=$(CPU) \
	-mstrict-align \
	-ffreestanding \
	-O3 \
	-MD \
	-MP \
	-Wall \
	-Wno-unused-function \
	-I$(BOARD_DIR)/include \
	-target $(TARGET) \
	-DBOARD_$(MICROKIT_BOARD) \
	-I$(LIONSOS)/include \
	-I$(SDDF)/include \
	-I$(CONFIG_INCLUDE)

LDFLAGS := -L$(BOARD_DIR)/lib
LIBS := -lmicrokit -Tmicrokit.ld libsddf_util_debug.a

IMAGE_FILE := capp.img
# IMAGE_FILE := webserver.img
REPORT_FILE := report.txt

all: $(IMAGE_FILE)
${IMAGES}: libsddf_util_debug.a

CHECK_FLAGS_BOARD_MD5:=.board_cflags-$(shell echo -- ${CFLAGS} ${BOARD} ${MICROKIT_CONFIG} | shasum | sed 's/ *-//')

${CHECK_FLAGS_BOARD_MD5}:
	-rm -f .board_cflags-*
	touch $@

backend.elf: 
	make -C $(LIONSOS)/components/microkitlibc -j$(nproc) \
			MICROKIT_SDK=$(MICROKIT_SDK) \
			MICROKIT_BOARD=$(MICROKIT_BOARD) \
			MICROKIT_CONFIG=$(MICROKIT_CONFIG) \
			BUILD=$(abspath .) \
			CONFIG_INCLUDE=$(abspath $(CONFIG_INCLUDE))

# micropython.elf: mpy-cross manifest.py webserver.py config.py \
# 		${MICRODOT} ${LIONSOS}/dep/libmicrokitco/Makefile
# 	make -C $(LIONSOS)/components/micropython -j$(nproc) \
# 			MICROKIT_SDK=$(MICROKIT_SDK) \
# 			MICROKIT_BOARD=$(MICROKIT_BOARD) \
# 			MICROKIT_CONFIG=$(MICROKIT_CONFIG) \
# 			MICROPY_MPYCROSS=$(abspath mpy_cross/mpy-cross) \
# 			MICROPY_MPYCROSS_DEPENDENCY=$(abspath mpy_cross/mpy-cross) \
# 			BUILD=$(abspath .) \
# 			LIBMATH=$(LIBMATH) \
# 			LIBMATH=$(abspath $(BUILD_DIR)/libm) \
# 			CONFIG_INCLUDE=$(abspath $(CONFIG_INCLUDE)) \
# 			FROZEN_MANIFEST=$(abspath ./manifest.py) \
# 			EXEC_MODULE=webserver.py

# config.py: ${CHECK_FLAGS_BOARD_MD5}
# 	echo "base_dir='$(WEBSITE_DIR)'" > config.py

# %.py: ${WEBSERVER_SRC_DIR}/%.py
# 	cp $< $@

$(MUSL)/lib/libc.a $(MUSL)/include: ${MUSL}/Makefile
	make -C $(MUSL_SRC) \
		C_COMPILER=aarch64-none-elf-gcc \
		TOOLPREFIX=aarch64-none-elf- \
		CONFIG_ARCH_AARCH64=y \
		STAGE_DIR=$(abspath $(MUSL)) \
		SOURCE_DIR=.

%.o: %.c
	${CC} ${CFLAGS} -c -o $@ $<

SDDF_MAKEFILES := ${SDDF}/util/util.mk \
		  ${SDDF}/drivers/timer/${TIMER_DRIVER_DIR}/timer_driver.mk \
		  ${SDDF}/drivers/network/${ETHERNET_DRIVER_DIR}/eth_driver.mk \
		  ${SDDF}/drivers/serial/${UART_DRIVER_DIR}/uart_driver.mk \
		  ${SDDF}/network/components/network_components.mk \
		  ${SDDF}/serial/components/serial_components.mk

include ${SDDF_MAKEFILES}
include $(NFS)/nfs.mk

$(IMAGE_FILE) $(REPORT_FILE): $(IMAGES) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

qemu: ${IMAGE_FILE}
	$(QEMU) -machine virt,virtualization=on \
			-cpu cortex-a53 \
			-serial mon:stdio \
			-device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0 \
			-m size=2G \
			-nographic \
			-device virtio-net-device,netdev=netdev0 \
			-netdev user,id=netdev0,hostfwd=tcp::5555-10.0.2.16:80 \
			-global virtio-mmio.force-legacy=false

FORCE: ;

# mpy-cross: FORCE  ${LIONSOS}/dep/micropython/mpy-cross
# 	make -C $(LIONSOS)/dep/micropython/mpy-cross BUILD=$(abspath ./mpy_cross)
# 
# .PHONY: mpy-cross

# $(LIONSOS)/dep/micropython/py/mkenv.mk ${LIONSOS}/dep/micropython/mpy-cross:
# 	cd ${LIONSOS}; git submodule update --init dep/micropython
# 	cd ${LIONSOS}/dep/micropython && git submodule update --init lib/micropython-lib
${LIONSOS}/dep/libmicrokitco/Makefile:
	cd ${LIONSOS}; git submodule update --init dep/libmicrokitco

# ${MICRODOT}:
# 	cd ${LIONSOS}; git submodule update --init dep/microdot

${MUSL}/Makefile:
	cd ${LIONSOS}; git submodule update --init dep/musllibc

${SDDF_MAKEFILES} &:
	cd ${LIONSOS}; git submodule update --init dep/sddf