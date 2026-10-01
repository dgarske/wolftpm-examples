/* mldsa_onboard.c
 *
 * The demo's central act, run entirely on the PSOC Control C3: the TPM
 * creates a Hash-ML-DSA key and signs a digest, and the MCU verifies that
 * signature itself with wolfCrypt. No host takes part in the cryptography.
 *
 * The event lines are the same newline-delimited JSON the host-side example
 * emits, deliberately, so the existing demo UI consumes them unchanged. That
 * grammar was designed to be reachable from a bare printf on an MCU, and this
 * is the case it was designed for.
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
#include <wolftpm/tpm2_wrap.h>
#include <wolfssl/wolfcrypt/sha256.h>
#include <wolfssl/wolfcrypt/wc_mldsa.h>

#include "hal/psoc_c3.h"

extern int TPM2_IoCb(TPM2_CTX* ctx, INT32 isRead, UINT32 addr, BYTE* buf,
    UINT16 size, void* userCtx);
extern void uart_init(void);
extern void uart_write(const char *buf, unsigned int sz);

#ifndef PSOC_C3_TPM_SEL_PORT
#define PSOC_C3_TPM_SEL_PORT    7
#endif
#ifndef PSOC_C3_TPM_SEL_PIN
#define PSOC_C3_TPM_SEL_PIN     7
#endif

/* Measured on this part; see the benchmark in this directory. */
#ifndef CPU_HZ
#define CPU_HZ 180000000UL
#endif

#define DEMCR       (*(volatile uint32_t *)0xE000EDFCUL)
#define DWT_CTRL    (*(volatile uint32_t *)0xE0001000UL)
#define DWT_CYCCNT  (*(volatile uint32_t *)0xE0001004UL)
#define DWT_LAR     (*(volatile uint32_t *)0xE0001FB0UL)

/* The message the host-side example signs, kept identical so the two
 * produce comparable output. */
static const char demo_msg[] =
    "wolfSSL + Infineon OktoberTech 2026 post-quantum TPM demo";

static WOLFTPM2_DEV dev;
static WOLFTPM2_KEY tpmKey;
static TPMT_PUBLIC tpl;
static MlDsaKey pubKey;
static byte sig[5000];
static byte digest[WC_SHA256_DIGEST_SIZE];

static void put(const char *s)
{
    unsigned int n = 0;
    while (s[n] != 0)
        n++;
    uart_write(s, n);
}

static void put_u32(uint32_t v)
{
    char tmp[12];
    char out[12];
    int t = 0, n = 0;

    if (v == 0) {
        out[n++] = '0';
    }
    else {
        while (v > 0) {
            tmp[t++] = (char)('0' + (v % 10));
            v /= 10;
        }
        while (t > 0)
            out[n++] = tmp[--t];
    }
    uart_write(out, (unsigned int)n);
}

static void put_hex32(uint32_t v)
{
    static const char d[] = "0123456789abcdef";
    char out[8];
    int i;

    for (i = 7; i >= 0; i--) {
        out[i] = d[v & 0xF];
        v >>= 4;
    }
    uart_write(out, 8);
}

static void cyc_init(void)
{
    DWT_LAR = 0xC5ACCE55UL;
    DEMCR |= (1UL << 24);
    DWT_CYCCNT = 0;
    DWT_CTRL |= 1UL;
}

static uint32_t ms_since(uint32_t start)
{
    return (DWT_CYCCNT - start) / (CPU_HZ / 1000UL);
}

/* Base64 three bytes at a time, with no buffer. The host-side emitter does
 * the same so a 4627 byte signature can be streamed out of a part that has
 * nowhere to stage the encoded form. */
static void emit_b64(const byte *b, uint32_t n)
{
    static const char t[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char q[4];
    uint32_t i;

    for (i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)b[i] << 16;
        uint32_t left = n - i;

        if (left > 1)
            v |= (uint32_t)b[i + 1] << 8;
        if (left > 2)
            v |= (uint32_t)b[i + 2];

        q[0] = t[(v >> 18) & 0x3F];
        q[1] = t[(v >> 12) & 0x3F];
        q[2] = (left > 1) ? t[(v >> 6) & 0x3F] : '=';
        q[3] = (left > 2) ? t[v & 0x3F] : '=';
        uart_write(q, 4);
    }
}

static void tpm_iface_select(void)
{
    psoc_c3_peri_init();
    psoc_c3_pin_setup(PSOC_C3_TPM_SEL_PORT, PSOC_C3_TPM_SEL_PIN, 0,
            GPIO_CFG_DM_STRONG);
    GPIO_PRT_OUT(PSOC_C3_TPM_SEL_PORT) &= ~(1UL << PSOC_C3_TPM_SEL_PIN);
}

static void fail(const char *what, int rc)
{
    put("{\"event\":\"error\",\"where\":\"");
    put(what);
    put("\",\"rc\":\"0x");
    put_hex32((uint32_t)rc);
    put("\"}\r\n");
}

void main(void)
{
    wc_Sha256 sha;
    uint32_t t0, signMs, verifyMs, keyMs;
    int sigSz, verifyRes = 0, rc;
    int tamper = 0;

    tpm_iface_select();
    uart_init();
    cyc_init();

    put("{\"event\":\"run.start\",\"host\":\"psoc_c3\",\"alg\":"
        "\"HASH_MLDSA\",\"param_set\":87}\r\n");

    rc = wolfTPM2_Init(&dev, TPM2_IoCb, NULL);
    if (rc != TPM_RC_SUCCESS) {
        fail("wolfTPM2_Init", rc);
        goto done;
    }
    put("{\"event\":\"tpm.startup\",\"ok\":true}\r\n");

    /* Hash-ML-DSA signs a digest the host computes, so the hashing happens
     * here and only the digest goes to the TPM. */
    rc = wc_InitSha256(&sha);
    if (rc == 0)
        rc = wc_Sha256Update(&sha, (const byte *)demo_msg,
                (word32)(sizeof(demo_msg) - 1));
    if (rc == 0)
        rc = wc_Sha256Final(&sha, digest);
    if (rc != 0) {
        fail("sha256", rc);
        goto cleanup;
    }

    t0 = DWT_CYCCNT;
    rc = wolfTPM2_GetKeyTemplate_HASH_MLDSA(&tpl,
            TPMA_OBJECT_sign | TPMA_OBJECT_fixedTPM |
            TPMA_OBJECT_fixedParent | TPMA_OBJECT_sensitiveDataOrigin |
            TPMA_OBJECT_userWithAuth | TPMA_OBJECT_noDA,
            TPM_MLDSA_87, TPM_ALG_SHA256);
    if (rc != 0) {
        fail("GetKeyTemplate_HASH_MLDSA", rc);
        goto cleanup;
    }
    rc = wolfTPM2_CreatePrimaryKey(&dev, &tpmKey, TPM_RH_OWNER, &tpl, NULL, 0);
    keyMs = ms_since(t0);
    if (rc != TPM_RC_SUCCESS) {
        fail("CreatePrimaryKey", rc);
        goto cleanup;
    }
    put("{\"event\":\"key.create\",\"alg\":\"HASH_MLDSA\",\"pub_bytes\":");
    put_u32(tpmKey.pub.publicArea.unique.mldsa.size);
    put(",\"ms\":"); put_u32(keyMs); put("}\r\n");

    sigSz = (int)sizeof(sig);
    t0 = DWT_CYCCNT;
    /* Empty signing context, which is what the host-side verify assumes. */
    rc = wolfTPM2_SignDigest(&dev, &tpmKey, digest, (int)sizeof(digest),
            NULL, 0, sig, &sigSz);
    signMs = ms_since(t0);
    if (rc != TPM_RC_SUCCESS) {
        fail("SignDigest", rc);
        goto unload;
    }
    put("{\"event\":\"sign.digest\",\"alg\":\"HASH_MLDSA\",\"param_set\":87,"
        "\"sig_bytes\":"); put_u32((uint32_t)sigSz);
    put(",\"ms\":"); put_u32(signMs); put("}\r\n");

    put("{\"event\":\"sign.bytes\",\"alg\":\"HASH_MLDSA\",\"bytes\":");
    put_u32((uint32_t)sigSz);
    put(",\"b64\":\"");
    emit_b64(sig, (uint32_t)sigSz);
    put("\"}\r\n");

    if (tamper) {
        sig[sigSz / 2] ^= 0x01;
        put("{\"event\":\"tamper\",\"offset\":");
        put_u32((uint32_t)(sigSz / 2)); put("}\r\n");
    }

    /* Verification happens here, on the MCU. Only the public key leaves the
     * TPM; the private half never does. */
    rc = wc_MlDsaKey_Init(&pubKey, NULL, INVALID_DEVID);
    if (rc == 0)
        rc = wc_MlDsaKey_SetParams(&pubKey, WC_ML_DSA_87);
    if (rc == 0)
        rc = wc_MlDsaKey_ImportPubRaw(&pubKey,
                tpmKey.pub.publicArea.unique.mldsa.buffer,
                tpmKey.pub.publicArea.unique.mldsa.size);
    if (rc != 0) {
        fail("import public key", rc);
        goto unload;
    }

    t0 = DWT_CYCCNT;
    rc = wc_MlDsaKey_VerifyCtxHash(&pubKey, sig, (word32)sigSz, NULL, 0,
            digest, (word32)sizeof(digest), WC_HASH_TYPE_SHA256, &verifyRes);
    verifyMs = ms_since(t0);
    if (rc != 0)
        verifyRes = 0;

    put("{\"event\":\"verify.host\",\"backend\":\"wolfcrypt\",\"on\":"
        "\"psoc_c3\",\"result\":\"");
    put(verifyRes ? "valid" : "invalid");
    put("\",\"ms\":"); put_u32(verifyMs); put("}\r\n");

    put("{\"event\":\"result\",\"pass\":");
    put((verifyRes != 0) == (tamper == 0) ? "true" : "false");
    put("}\r\n");

    wc_MlDsaKey_Free(&pubKey);

unload:
    wolfTPM2_UnloadHandle(&dev, &tpmKey.handle);
cleanup:
    wolfTPM2_Cleanup(&dev);
done:
    put("{\"event\":\"run.end\"}\r\n");
    while (1)
        ;
}
