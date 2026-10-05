/* tpm_io_uart_gateway.h
 *
 * wolfTPM IO callback that reaches a TPM 2.0 through the PSOC Control C3
 * UART-to-I2C gateway. Build wolfTPM with WOLFTPM_I2C and WOLFTPM_ADV_IO:
 * the gateway speaks TIS registers, which is what the advanced callback form
 * reports.
 *
 * Copyright (C) 2006-2026 wolfSSL Inc.
 *
 * This file is part of wolfTPM.
 *
 * wolfTPM is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * wolfTPM is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */
#ifndef TPM_IO_UART_GATEWAY_H_INCLUDED
#define TPM_IO_UART_GATEWAY_H_INCLUDED

#include <wolftpm/tpm2.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Open the gateway. "port" is a device path on POSIX ("/dev/ttyACM0") or a
 * port name on Windows ("COM12"); NULL takes WOLFTPM_GW_PORT from the
 * environment. Returns 0 on success. */
int TPM2_UartGateway_Open(const char* port, unsigned int baud);

/* Close it. Safe to call when never opened. */
void TPM2_UartGateway_Close(void);

/* Verify the gateway answers and reports the TPM present. Returns 0 if so. */
int TPM2_UartGateway_Ping(char* idOut, int idOutSz);

/* Pass to wolfTPM_Init() as the IO callback. */
int TPM2_IoCb_UartGateway(TPM2_CTX* ctx, int isRead, word32 addr, byte* buf,
    word16 size, void* userCtx);

#ifdef __cplusplus
}
#endif

#endif /* TPM_IO_UART_GATEWAY_H_INCLUDED */
