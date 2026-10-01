# Makefile
#
# Build wolfTPM examples so they reach the TPM through the gateway.
#
# Nothing in wolfTPM is modified. The examples are its own sources, linked
# against its static library with gw_tpm_iocb.c supplying TPM2_IoCb; the
# library's own HAL object is then never pulled in.
#
# Point WOLFTPM_DIR at a wolfTPM tree configured with:
#   --enable-i2c --enable-advio --enable-v185 --disable-autodetect \
#   --enable-static --disable-shared --with-wolfcrypt=<wolfssl prefix>
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

WOLFTPM_DIR ?= $(HOME)/GitHub/wolftpm-worktrees/demo
WOLFSSL_DIR ?= $(HOME)/Projects/Infineon/wolfssl-pqc-gateway
OUT         ?= bin

TPMLIB  = $(WOLFTPM_DIR)/src/.libs/libwolftpm.a
CFLAGS  += -Wall -I$(WOLFTPM_DIR) -I$(WOLFTPM_DIR)/examples -Ihost \
           -I$(WOLFSSL_DIR)/include
LDLIBS  += $(TPMLIB) -L$(WOLFSSL_DIR)/lib -lwolfssl -lm
GW       = host/tpm_io_uart_gateway.c host/gw_tpm_iocb.c

# Each demo act, as the example source that implements it.
EXAMPLES = \
  caps:wrap/caps.c \
  mldsa_host_verify:pqc/mldsa_host_verify.c \
  get_ek_certs:endorsement/get_ek_certs.c \
  seal_pcr:seal/seal_pcr.c \
  pcr_extend:pcr/extend.c \
  pcr_reset:pcr/reset.c \
  pcr_read:pcr/read_pcr.c

SUPPORT = $(WOLFTPM_DIR)/examples/tpm_test_keys.c

.PHONY: all clean
all:
	@mkdir -p $(OUT)
	@for e in $(EXAMPLES); do \
	  n=$${e%%:*}; s=$${e#*:}; \
	  printf '  %-20s ' "$$n"; \
	  $(CC) $(CFLAGS) -o $(OUT)/$$n $(WOLFTPM_DIR)/examples/$$s $(SUPPORT) \
	      $(GW) $(LDLIBS) 2>$(OUT)/$$n.log && echo ok || echo "FAILED (see $(OUT)/$$n.log)"; \
	done

clean:
	rm -rf $(OUT)