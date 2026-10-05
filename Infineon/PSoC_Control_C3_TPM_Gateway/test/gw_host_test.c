/* gw_host_test.c
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
#include <time.h>
#include <stdlib.h>
#include "../host/tpm_io_uart_gateway.h"

static int fails;

static void check(int cond, const char *what)
{
    printf("%-46s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond)
        fails++;
}

int main(int argc, char **argv)
{
    unsigned char wr[16], rd[16];
    char id[64];
    int i;

    if (argc < 2) {
        printf("usage: %s <pty> [resync]\n", argv[0]);
        return 2;
    }
    check(TPM2_UartGateway_Open(argv[1], (argc>3)?(unsigned)atoi(argv[3]):115200) == 0, "open gateway");

    /* With the model damaging its first responses, the retry budget has to
     * absorb them: a booth run must survive a corrupted frame, not stop. */
    if ((argc > 2) && (strcmp(argv[2], "resync") == 0)) {
        for (i = 0; i < (int)sizeof(wr); i++)
            wr[i] = (unsigned char)(0x10 + i);
        check(TPM2_IoCb_UartGateway(NULL, 0, 0x30, wr, sizeof(wr), NULL)
                == TPM_RC_SUCCESS, "write survives corrupted responses");
        memset(rd, 0, sizeof(rd));
        check(TPM2_IoCb_UartGateway(NULL, 1, 0x30, rd, sizeof(rd), NULL)
                == TPM_RC_SUCCESS, "read after resync");
        check(memcmp(wr, rd, sizeof(wr)) == 0, "data intact after resync");
        check(TPM2_UartGateway_Ping(id, sizeof(id)) == 0, "ping after resync");
        TPM2_UartGateway_Close();
        printf("%s\n", fails ? "FAILURES" : "all gateway resync tests passed");
        return fails ? 1 : 0;
    }

    check(TPM2_UartGateway_Ping(id, sizeof(id)) == 0, "ping answers");
    check(strcmp(id, "PSOC-C3-TPM-GW/1") == 0, "ping reports gateway identity");

    /* One ping is one round trip with a minimal payload, so it isolates
     * per-transaction latency from line rate. */
    if ((argc > 2) && (strcmp(argv[2], "bench") == 0)) {
        struct timespec t0, t1;
        double ms;
        int n = 200;

        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (i = 0; i < n; i++) {
            if (TPM2_UartGateway_Ping(NULL, 0) != 0) {
                printf("ping %d failed\n", i);
                break;
            }
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms = ((t1.tv_sec - t0.tv_sec) * 1000.0)
           + ((t1.tv_nsec - t0.tv_nsec) / 1000000.0);
        printf("%d round trips in %.0f ms = %.2f ms each\n", n, ms, ms / n);
        TPM2_UartGateway_Close();
        return 0;
    }

    /* Against real silicon the data FIFO does not read back what was written
     * to it, so identity is only meaningful against the model. */
    if ((argc > 2) && (strcmp(argv[2], "hw") == 0)) {
        unsigned char id4[4];

        memset(id4, 0, sizeof(id4));
        check(TPM2_IoCb_UartGateway(NULL, 1, 0x48, id4, 4, NULL)
                == TPM_RC_SUCCESS, "read TIS DID_VID over the gateway");
        printf("   DID_VID = %02x %02x %02x %02x  (vid %04x, did %04x)\n",
                id4[0], id4[1], id4[2], id4[3],
                (unsigned)(id4[0] | (id4[1] << 8)),
                (unsigned)(id4[2] | (id4[3] << 8)));
        check((unsigned)(id4[0] | (id4[1] << 8)) == 0x15d1,
                "vendor ID is Infineon (0x15d1)");
        TPM2_UartGateway_Close();
        printf("%s\n", fails ? "FAILURES" : "all gateway hardware tests passed");
        return fails ? 1 : 0;
    }


    for (i = 0; i < (int)sizeof(wr); i++)
        wr[i] = (unsigned char)(0xA0 + i);
    check(TPM2_IoCb_UartGateway(NULL, 0, 0x24, wr, sizeof(wr), NULL)
            == TPM_RC_SUCCESS, "write 16 bytes to a TIS register");

    memset(rd, 0, sizeof(rd));
    check(TPM2_IoCb_UartGateway(NULL, 1, 0x24, rd, sizeof(rd), NULL)
            == TPM_RC_SUCCESS, "read them back");
    check(memcmp(wr, rd, sizeof(wr)) == 0, "read matches write");

    /* A transfer at the frame limit is the one most likely to be off by one. */
    check(TPM2_IoCb_UartGateway(NULL, 1, 0x00, rd, 1, NULL)
            == TPM_RC_SUCCESS, "single byte read");
    check(TPM2_IoCb_UartGateway(NULL, 1, 0x00, rd, 65, NULL)
            == BAD_FUNC_ARG, "oversize transfer refused");

    TPM2_UartGateway_Close();
    printf("%s\n", fails ? "FAILURES" : "all gateway host tests passed");
    return fails ? 1 : 0;
}
