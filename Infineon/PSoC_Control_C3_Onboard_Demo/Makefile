# Makefile
#
# Build and sign the ML-DSA verification benchmark for wolfBoot on
# PSOC Control C3. See the gateway example for the wolfBoot build; this
# reuses its bootloader, signing key and linker script.
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
WOLFSSL_DIR  ?= $(HOME)/wolfssl
SIGN_ALG     ?= --ecc256
APP_VERSION  ?= 1
APP_ADDR     ?= 0x12010100
APP_SIZE     ?= 0x16F00

CROSS   ?= arm-none-eabi-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy
SIZE     = $(CROSS)size

DEFS = -DTARGET_psoc_c3 -DWOLFSSL_USER_SETTINGS \
       -DPSOC_C3_UART_SCB=3 -DPSOC_C3_UART_PORT=6 \
       -DPSOC_C3_UART_PCLK_GR=4 -DPSOC_C3_UART_PCLK_IDX=6 \
       -DPSOC_C3_PCLK_HZ=48000000

CFLAGS  += -mcpu=cortex-m33 -mthumb -Os -Wall \
           -ffreestanding -ffunction-sections -fdata-sections -fno-common \
           -I. -I$(WOLFBOOT_DIR) -I$(WOLFBOOT_DIR)/include \
           -I$(WOLFSSL_DIR) $(DEFS)
LDFLAGS += -mcpu=cortex-m33 -mthumb -nostartfiles -ffreestanding \
           -Wl,-gc-sections -Wl,-Map=mldsa_bench.map -T mldsa_bench.ld

WC = $(WOLFSSL_DIR)/wolfcrypt/src
SRCS = mldsa_bench.c \
       $(WC)/wc_mldsa.c $(WC)/sha3.c $(WC)/hash.c $(WC)/sha256.c \
       $(WC)/memory.c $(WC)/wc_port.c $(WC)/logging.c $(WC)/misc.c $(WC)/asn.c $(WC)/wolfmath.c $(WC)/sp_int.c \
       $(WOLFBOOT_DIR)/hal/psoc_c3.c \
       $(WOLFBOOT_DIR)/test-app/startup_arm.c

.PHONY: all clean size
all: mldsa_bench_v$(APP_VERSION)_signed.bin size

mldsa_bench.ld: $(WOLFBOOT_DIR)/test-app/ARM-psoc_c3.ld
	sed -e "s/@WOLFBOOT_TEST_APP_ADDRESS@/$(APP_ADDR)/g" \
	    -e "s/@WOLFBOOT_TEST_APP_SIZE@/$(APP_SIZE)/g" $< > $@

mldsa_bench.elf: $(SRCS) mldsa_bench.ld
	$(CC) $(CFLAGS) $(SRCS) $(LDFLAGS) -o $@

mldsa_bench.bin: mldsa_bench.elf
	$(OBJCOPY) -O binary $< $@

mldsa_bench_v$(APP_VERSION)_signed.bin: mldsa_bench.bin
	$(WOLFBOOT_DIR)/tools/keytools/sign $(SIGN_ALG) $< \
	    $(WOLFBOOT_DIR)/wolfboot_signing_private_key.der $(APP_VERSION)

size: mldsa_bench.elf
	@$(SIZE) $<

clean:
	rm -f mldsa_bench.elf mldsa_bench.bin mldsa_bench.ld mldsa_bench.map \
	      mldsa_bench_v*_signed.bin

# ---------------------------------------------------------------------------
# On-board TPM identity read. Drives the TPM from the MCU over I2C with no
# host involved, using wolfBoot's I2C driver and a stock wolfTPM.
# ---------------------------------------------------------------------------
WOLFTPM_DIR ?= $(HOME)/wolfTPM
TPM_DEFS = -DTARGET_psoc_c3 -DWOLFBOOT_TPM_I2C -DWOLFSSL_USER_SETTINGS \
           -DWOLFTPM_USER_SETTINGS \
           -DPSOC_C3_UART_SCB=3 -DPSOC_C3_UART_PORT=6 \
           -DPSOC_C3_UART_PCLK_GR=4 -DPSOC_C3_UART_PCLK_IDX=6 \
           -DPSOC_C3_PCLK_HZ=48000000 \
           -DPSOC_C3_I2C_SCB=0 -DPSOC_C3_I2C_PORT=9 \
           -DPSOC_C3_I2C_SCL_PIN=0 -DPSOC_C3_I2C_SDA_PIN=2 \
           -DPSOC_C3_I2C_HSIOM_SEL=15 -DPSOC_C3_I2C_HZ=100000

TPM_CFLAGS = -mcpu=cortex-m33 -mthumb -Os -Wall \
             -ffreestanding -ffunction-sections -fdata-sections -fno-common \
             -I. -I$(WOLFBOOT_DIR) -I$(WOLFBOOT_DIR)/include \
             -I$(WOLFSSL_DIR) -I$(WOLFTPM_DIR) $(TPM_DEFS) \
             -include user_settings_tpm.h

WT = $(WOLFTPM_DIR)/src
TPM_SRCS = tpm_identity.c tpm_io_i2c_c3.c \
           $(WT)/tpm2.c $(WT)/tpm2_packet.c $(WT)/tpm2_tis.c \
           $(WT)/tpm2_wrap.c $(WT)/tpm2_crypto.c $(WT)/tpm2_util.c \
           $(WT)/tpm2_param_enc.c \
           $(WC)/sha256.c $(WC)/sha512.c $(WC)/hash.c $(WC)/memory.c \
           $(WC)/wc_port.c $(WC)/logging.c $(WC)/misc.c $(WC)/asn.c \
           $(WC)/coding.c $(WC)/aes.c $(WC)/hmac.c $(WC)/random.c \
           $(WC)/ecc.c $(WC)/sp_int.c $(WC)/sp_cortexm.c $(WC)/wolfmath.c \
           $(WOLFBOOT_DIR)/hal/psoc_c3.c \
           $(WOLFBOOT_DIR)/hal/i2c/i2c_drv_psoc_c3.c \
           $(WOLFBOOT_DIR)/test-app/startup_arm.c

.PHONY: tpm tpm-clean
tpm: tpm_identity.bin
	@$(SIZE) tpm_identity.elf

tpm_identity.ld: $(WOLFBOOT_DIR)/test-app/ARM-psoc_c3.ld
	sed -e "s/@WOLFBOOT_TEST_APP_ADDRESS@/$(APP_ADDR)/g" \
	    -e "s/@WOLFBOOT_TEST_APP_SIZE@/$(APP_SIZE)/g" $< > $@

tpm_identity.elf: $(TPM_SRCS) tpm_identity.ld
	$(CC) $(TPM_CFLAGS) $(TPM_SRCS) -mcpu=cortex-m33 -mthumb -nostartfiles \
	    -ffreestanding -Wl,-gc-sections -Wl,-Map=tpm_identity.map \
	    -T tpm_identity.ld -o $@

tpm_identity.bin: tpm_identity.elf
	$(OBJCOPY) -O binary $< $@

tpm-clean:
	rm -f tpm_identity.elf tpm_identity.bin tpm_identity.ld tpm_identity.map

# ---------------------------------------------------------------------------
# The sign-and-verify act, run entirely on the board.
# ---------------------------------------------------------------------------
ONB_SRCS = mldsa_onboard.c tpm_io_i2c_c3.c heap.c \
           $(WT)/tpm2.c $(WT)/tpm2_packet.c $(WT)/tpm2_tis.c \
           $(WT)/tpm2_wrap.c $(WT)/tpm2_crypto.c $(WT)/tpm2_util.c \
           $(WT)/tpm2_param_enc.c \
           $(WC)/sha256.c $(WC)/sha512.c $(WC)/sha3.c $(WC)/hash.c \
           $(WC)/memory.c $(WC)/wc_port.c $(WC)/logging.c $(WC)/misc.c \
           $(WC)/asn.c $(WC)/coding.c $(WC)/aes.c $(WC)/hmac.c \
           $(WC)/random.c $(WC)/ecc.c $(WC)/sp_int.c $(WC)/sp_cortexm.c \
           $(WC)/wolfmath.c $(WC)/wc_mldsa.c \
           $(WOLFBOOT_DIR)/hal/psoc_c3.c \
           $(WOLFBOOT_DIR)/hal/i2c/i2c_drv_psoc_c3.c \
           $(WOLFBOOT_DIR)/test-app/startup_arm.c

.PHONY: onboard onboard-clean
onboard: mldsa_onboard.bin
	@$(SIZE) mldsa_onboard.elf

mldsa_onboard.ld: $(WOLFBOOT_DIR)/test-app/ARM-psoc_c3.ld
	sed -e "s/@WOLFBOOT_TEST_APP_ADDRESS@/$(APP_ADDR)/g" \
	    -e "s/@WOLFBOOT_TEST_APP_SIZE@/$(APP_SIZE)/g" $< > $@

mldsa_onboard.elf: $(ONB_SRCS) mldsa_onboard.ld
	$(CC) $(TPM_CFLAGS) $(ONB_SRCS) -mcpu=cortex-m33 -mthumb -nostartfiles \
	    -ffreestanding -Wl,-gc-sections -Wl,-Map=mldsa_onboard.map \
	    -T mldsa_onboard.ld -o $@

mldsa_onboard.bin: mldsa_onboard.elf
	$(OBJCOPY) -O binary $< $@

onboard-clean:
	rm -f mldsa_onboard.elf mldsa_onboard.bin mldsa_onboard.ld mldsa_onboard.map
