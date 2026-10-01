/* gw_testresult.c
 *
 * few commands a failed TPM still answers, and it carries the vendor's reason
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
#include <string.h>
#include <wolftpm/tpm2.h>
#include "tpm_io_uart_gateway.h"

int main(int argc, char** argv)
{
    TPM2_CTX ctx;
    GetTestResult_Out out;
    Startup_In startup;
    int rc;

    memset(&ctx, 0, sizeof(ctx));
    memset(&out, 0, sizeof(out));
    if (TPM2_UartGateway_Open(argc > 1 ? argv[1] : NULL, 0) != 0) {
        printf("cannot open gateway\n");
        return 1;
    }
    rc = TPM2_Init_ex(&ctx, TPM2_IoCb_UartGateway, NULL, 0);
    printf("TPM2_Init         : 0x%x %s\n", rc, TPM2_GetRCString(rc));
    if (rc != TPM_RC_SUCCESS) { TPM2_UartGateway_Close(); return 1; }

    rc = TPM2_GetTestResult(&out);
    printf("TPM2_GetTestResult: 0x%x %s\n", rc, TPM2_GetRCString(rc));
    if (rc == TPM_RC_SUCCESS) {
        printf("  testResult      : 0x%x %s\n", out.testResult,
            TPM2_GetRCString(out.testResult));
        printf("  outData size    : %d\n", out.outData.size);
        if (out.outData.size > 0) {
            int i;
            printf("  outData         : ");
            for (i = 0; i < out.outData.size && i < 32; i++)
                printf("%02X", out.outData.buffer[i]);
            printf("\n");
        }
    }

    memset(&startup, 0, sizeof(startup));
    startup.startupType = TPM_SU_CLEAR;
    rc = TPM2_Startup(&startup);
    printf("TPM2_Startup CLEAR: 0x%x %s\n", rc, TPM2_GetRCString(rc));

    TPM2_UartGateway_Close();
    return 0;
}
