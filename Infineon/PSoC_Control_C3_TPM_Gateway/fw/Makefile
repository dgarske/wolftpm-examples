# Makefile
#
# Build and sign the gateway application for wolfBoot on PSOC Control C3.
#
# wolfBoot is built separately and only as the bootloader:
#
#   cd <wolfboot>
#   cp config/examples/psoc_c3.config .config
#   make wolfboot.elf WOLFTPM=1 WOLFBOOT_TPM_I2C=1
#
# That also generates the signing key and include/target.h, both used here.
# WOLFTPM_TPM_I2C matters for the bootloader even though this application
# does its own TPM work: the TPM latches its bus from a strap while its own
# reset is low, which is long before a verified application can run, so
# wolfBoot's hal_init() is the only thing early enough to drive it.
#
# Copyright (C) 2006-2026 wolfSSL Inc.
#
# This file is part of wolfTPM.
#
# wolfTPM is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# wolfTPM is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA

WOLFBOOT_DIR ?= $(HOME)/wolfBoot
SIGN_ALG     ?= --ecc256
APP_VERSION  ?= 1

# Application load address and extent: the boot partition start plus the
# image header, and the partition size less that header.
APP_ADDR ?= 0x12010100
APP_SIZE ?= 0x16F00

CROSS   ?= arm-none-eabi-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy

# Board wiring. These match the defaults in wolfBoot's arch.mk and are spelled
# out rather than inherited so the example documents its own pinout.
DEFS = -DTARGET_psoc_c3 -DWOLFBOOT_TPM_I2C \
       -DPSOC_C3_UART_SCB=3 -DPSOC_C3_UART_PORT=6 \
       -DPSOC_C3_UART_PCLK_GR=4 -DPSOC_C3_UART_PCLK_IDX=6 \
       -DPSOC_C3_PCLK_HZ=48000000 \
       -DPSOC_C3_I2C_SCB=0 -DPSOC_C3_I2C_PORT=9 \
       -DPSOC_C3_I2C_SCL_PIN=0 -DPSOC_C3_I2C_SDA_PIN=2 \
       -DPSOC_C3_I2C_HSIOM_SEL=15 -DPSOC_C3_I2C_HZ=100000

CFLAGS  += -mcpu=cortex-m33 -mthumb -Os -Wall -Wextra \
           -ffreestanding -ffunction-sections -fdata-sections -fno-common \
           -I$(WOLFBOOT_DIR) -I$(WOLFBOOT_DIR)/include -I. $(DEFS)
LDFLAGS += -mcpu=cortex-m33 -mthumb -nostartfiles -ffreestanding \
           -Wl,-gc-sections -Wl,-Map=gateway.map -T gateway.ld

SRCS = gateway.c \
       $(WOLFBOOT_DIR)/hal/psoc_c3.c \
       $(WOLFBOOT_DIR)/hal/i2c/i2c_drv_psoc_c3.c \
       $(WOLFBOOT_DIR)/test-app/startup_arm.c

.PHONY: all clean
all: gateway_v$(APP_VERSION)_signed.bin

# The shipped linker script is a template; fill in where the application lives.
gateway.ld: $(WOLFBOOT_DIR)/test-app/ARM-psoc_c3.ld
	sed -e "s/@WOLFBOOT_TEST_APP_ADDRESS@/$(APP_ADDR)/g" \
	    -e "s/@WOLFBOOT_TEST_APP_SIZE@/$(APP_SIZE)/g" $< > $@

gateway.elf: $(SRCS) gateway.ld
	$(CC) $(CFLAGS) $(SRCS) $(LDFLAGS) -o $@

gateway.bin: gateway.elf
	$(OBJCOPY) -O binary $< $@

gateway_v$(APP_VERSION)_signed.bin: gateway.bin
	$(WOLFBOOT_DIR)/tools/keytools/sign $(SIGN_ALG) $< \
	    $(WOLFBOOT_DIR)/wolfboot_signing_private_key.der $(APP_VERSION)

clean:
	rm -f gateway.elf gateway.bin gateway.ld gateway.map gateway_v*_signed.bin