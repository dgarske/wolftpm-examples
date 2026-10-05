/* gw_win_test.c
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tpm_io_uart_gateway.h"

int main(int argc, char** argv)
{
    unsigned char id4[4];
    char id[64];

    if (argc < 2) {
        printf("usage: %s <COMn> [baud]\n", argv[0]);
        return 2;
    }
    if (TPM2_UartGateway_Open(argv[1], (argc > 2) ? (unsigned)atoi(argv[2]) : 115200) != 0) {
        printf("FAIL: cannot open %s\n", argv[1]);
        return 1;
    }
    if (TPM2_UartGateway_Ping(id, sizeof(id)) != 0) {
        printf("FAIL: no ping response\n");
        TPM2_UartGateway_Close();
        return 1;
    }
    printf("gateway: %s\n", id);
    memset(id4, 0, sizeof(id4));
    if (TPM2_IoCb_UartGateway(NULL, 1, 0x48, id4, 4, NULL) != TPM_RC_SUCCESS) {
        printf("FAIL: DID_VID read\n");
        TPM2_UartGateway_Close();
        return 1;
    }
    printf("DID_VID = %02x %02x %02x %02x  (vid %04x, did %04x)\n",
        id4[0], id4[1], id4[2], id4[3],
        (unsigned)(id4[0] | (id4[1] << 8)), (unsigned)(id4[2] | (id4[3] << 8)));
    TPM2_UartGateway_Close();
    return 0;
}
