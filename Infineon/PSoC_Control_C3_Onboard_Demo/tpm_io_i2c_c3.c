/* tpm_io_i2c_c3.c
 *
 * wolfTPM IO callback driving the TPM directly from the PSOC Control C3 over
 * I2C, using wolfBoot's I2C driver.
 *
 * This is the on-board counterpart to the gateway example: there the MCU
 * forwarded register transfers to a host, here it performs the whole TPM
 * exchange itself and no host is involved in the cryptography.
 *
 * The transfer sequence is wolfBoot's, not a rewrite. The register write is
 * followed by a stop and a guard delay rather than a repeated start, because
 * the part NAKs briefly after being addressed while it wakes and the
 * specification asks for a guard time that a repeated start cannot provide.
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
#include <stdint.h>
#include <string.h>

#include <wolftpm/tpm2.h>

#include "i2c_drv.h"

#ifndef TPM2_I2C_ADDR
#define TPM2_I2C_ADDR       0x2E    /* 7-bit TCG TIS address */
#endif
#ifndef TPM_I2C_TRIES
#define TPM_I2C_TRIES       10
#endif
#ifndef TPM_I2C_GUARD_LOOPS
#define TPM_I2C_GUARD_LOOPS 20000
#endif

static int i2c_ready;

static void tpm_i2c_guard(void)
{
    volatile uint32_t i;

    for (i = 0; i < (uint32_t)TPM_I2C_GUARD_LOOPS; i++)
        ;
}

static int tpm_i2c_read(uint8_t reg, uint8_t* data, uint32_t len)
{
    uint8_t regbuf = reg;
    int tries = TPM_I2C_TRIES;
    int ret;

    do {
        ret = i2c_write(TPM2_I2C_ADDR, &regbuf, 1, 1);
        tpm_i2c_guard();
    } while ((ret != I2C_OK) && (--tries > 0));

    if (ret != I2C_OK)
        return ret;

    tries = TPM_I2C_TRIES;
    do {
        ret = i2c_read(TPM2_I2C_ADDR, data, len, 1);
        if (ret != I2C_OK)
            tpm_i2c_guard();
    } while ((ret != I2C_OK) && (--tries > 0));

    return ret;
}

/* One transfer: the register byte followed by the payload. */
static int tpm_i2c_write(uint8_t reg, const uint8_t* data, uint32_t len)
{
    static uint8_t buf[MAX_SPI_FRAMESIZE + 1];
    int tries = TPM_I2C_TRIES;
    int ret;

    if (len > MAX_SPI_FRAMESIZE)
        return I2C_ERR_ARG;

    buf[0] = reg;
    memcpy(&buf[1], data, len);

    do {
        ret = i2c_write(TPM2_I2C_ADDR, buf, len + 1, 1);
        if (ret != I2C_OK)
            tpm_i2c_guard();
    } while ((ret != I2C_OK) && (--tries > 0));

    /* A command payload can carry an authValue; do not leave it behind. */
    TPM2_ForceZero(buf, sizeof(buf));
    return ret;
}

int TPM2_IoCb(TPM2_CTX* ctx, INT32 isRead, UINT32 addr, BYTE* buf,
    UINT16 size, void* userCtx)
{
    int ret;

    (void)ctx;
    (void)userCtx;

    if (!i2c_ready) {
        i2c_init();
        i2c_ready = 1;
    }

    if (isRead)
        ret = tpm_i2c_read((uint8_t)(addr & 0xFF), buf, (uint32_t)size);
    else
        ret = tpm_i2c_write((uint8_t)(addr & 0xFF), buf, (uint32_t)size);

    return (ret == I2C_OK) ? TPM_RC_SUCCESS : TPM_RC_FAILURE;
}
