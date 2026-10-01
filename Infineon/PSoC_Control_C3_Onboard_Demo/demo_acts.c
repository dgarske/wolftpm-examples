/* demo_acts.c
 *
 * The demo's acts, run on the PSOC Control C3 and selected by a command byte
 * on the console. The host writes one character and reads back the events,
 * so a host-side demo server becomes a serial reader rather than something
 * that spawns programs and parses their output.
 *
 * Events are the same newline-delimited JSON the host-side examples emit, so
 * an existing UI consumes them unchanged.
 *
 *   i  identity        read the TPM's vendor and firmware
 *   p  measured boot   read the PCR bank
 *   l  sealed secret   seal to a PCR, unseal, extend, fail, reset
 *   e  endorsement     read the EK certificates out of NV
 *
 * The signing act lives in act_sign.c and is selected with 's'.
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

#include "demo_util.h"
#include "act_sign.h"

extern int TPM2_IoCb(TPM2_CTX* ctx, INT32 isRead, UINT32 addr, BYTE* buf,
    UINT16 size, void* userCtx);

/* PCR 16 is the debug-use index, resettable from software, which is what
 * lets the sealed-secret act put the bank back afterwards. */
#ifndef DEMO_SEAL_PCR
#define DEMO_SEAL_PCR 16
#endif
#define DEMO_PCR_COUNT 24

static const char seal_secret[] = "OktoberTech2026";

static WOLFTPM2_DEV dev;

static void act_identity(void)
{
    WOLFTPM2_CAPS caps;
    int rc;

    memset(&caps, 0, sizeof(caps));
    rc = wolfTPM2_GetCapabilities(&dev, &caps);
    if (rc != TPM_RC_SUCCESS) {
        demo_fail("GetCapabilities", rc);
        return;
    }
    demo_put("{\"event\":\"tpm.identity\",\"mfg\":\"");
    demo_put(caps.mfgStr);
    demo_put("\",\"vendor\":\"");
    demo_put(caps.vendorStr);
    demo_put("\",\"fw\":\"");
    demo_put_u32(caps.fwVerMajor);
    demo_put(".");
    demo_put_u32(caps.fwVerMinor);
    demo_put("\",\"on\":\"psoc_c3\"}\r\n");
}

static int read_pcr(int index, byte *digest, int *digestSz)
{
    return wolfTPM2_ReadPCR(&dev, index, TPM_ALG_SHA256, digest, digestSz);
}

static void act_pcr(void)
{
    byte digest[TPM_SHA256_DIGEST_SIZE];
    int digestSz, i, rc;

    demo_put("{\"event\":\"pcr.begin\",\"bank\":\"SHA256\"}\r\n");
    for (i = 0; i < DEMO_PCR_COUNT; i++) {
        digestSz = 0;
        memset(digest, 0, sizeof(digest));
        rc = read_pcr(i, digest, &digestSz);
        if (rc != TPM_RC_SUCCESS) {
            demo_fail("ReadPCR", rc);
            break;
        }
        demo_put("{\"event\":\"pcr.value\",\"index\":");
        demo_put_u32((uint32_t)i);
        demo_put(",\"digest\":\"");
        demo_put_hexbuf(digest, (uint32_t)digestSz);
        demo_put("\"}\r\n");
    }
    demo_put("{\"event\":\"pcr.end\"}\r\n");
}

/* Extend the demo PCR with a fixed value, which is what "the measurement
 * changed" means in the sealed-secret act. */
static int extend_pcr(int index)
{
    byte data[TPM_SHA256_DIGEST_SIZE];

    memset(data, 0x5A, sizeof(data));
    return wolfTPM2_ExtendPCR(&dev, index, TPM_ALG_SHA256, data,
            (int)sizeof(data));
}

/* Load the sealed blob and unseal it under a PCR policy session. The policy
 * is evaluated against the PCR bank as it stands now, so this succeeds only
 * while the measurement still matches the one sealed to. */
static int try_unseal(WOLFTPM2_KEY *primary, WOLFTPM2_KEYBLOB *seal,
    byte *pcrArray, byte *out, int *outSz)
{
    WOLFTPM2_SESSION policy;
    WOLFTPM2_KEY sealed;
    Unseal_In in;
    Unseal_Out unsealed;
    int rc;

    memset(&policy, 0, sizeof(policy));
    memset(&sealed, 0, sizeof(sealed));
    memset(&in, 0, sizeof(in));
    memset(&unsealed, 0, sizeof(unsealed));

    rc = wolfTPM2_LoadKey(&dev, seal, &primary->handle);
    if (rc != TPM_RC_SUCCESS)
        return rc;
    sealed.handle = seal->handle;

    rc = wolfTPM2_StartSession(&dev, &policy, NULL, NULL,
            TPM_SE_POLICY, TPM_ALG_NULL);
    if (rc != TPM_RC_SUCCESS)
        goto unload;

    rc = wolfTPM2_PolicyPCR(&dev, policy.handle.hndl, TPM_ALG_SHA256,
            pcrArray, 1);
    if (rc == TPM_RC_SUCCESS)
        rc = wolfTPM2_SetAuthSession(&dev, 0, &policy, 0);
    if (rc != TPM_RC_SUCCESS)
        goto unload_session;

    in.itemHandle = sealed.handle.hndl;
    rc = TPM2_Unseal(&in, &unsealed);
    if (rc == TPM_RC_SUCCESS) {
        if ((int)unsealed.outData.size < *outSz)
            *outSz = (int)unsealed.outData.size;
        memcpy(out, unsealed.outData.buffer, (size_t)*outSz);
        out[*outSz] = 0;
    }

    wolfTPM2_UnsetAuth(&dev, 0);
unload_session:
    wolfTPM2_UnloadHandle(&dev, &policy.handle);
unload:
    wolfTPM2_UnloadHandle(&dev, &sealed.handle);
    return rc;
}

static void act_seal(void)
{
    WOLFTPM2_KEY primary;
    WOLFTPM2_KEYBLOB seal;
    byte digest[TPM_SHA256_DIGEST_SIZE];
    byte out[sizeof(seal_secret) + 1];
    byte pcrArray[1];
    TPMT_PUBLIC tpl;
    int digestSz = 0, outSz, rc;

    memset(&primary, 0, sizeof(primary));
    memset(&seal, 0, sizeof(seal));

    demo_put("{\"event\":\"seal.begin\",\"pcr\":");
    demo_put_u32(DEMO_SEAL_PCR);
    demo_put(",\"secret\":\"");
    demo_put(seal_secret);
    demo_put("\"}\r\n");

    /* Start from a known measurement so the act is repeatable. */
    rc = wolfTPM2_ResetPCR(&dev, DEMO_SEAL_PCR);
    if (rc == TPM_RC_SUCCESS)
        rc = read_pcr(DEMO_SEAL_PCR, digest, &digestSz);
    if (rc != TPM_RC_SUCCESS) {
        demo_fail("reset PCR", rc);
        return;
    }
    demo_put("{\"event\":\"seal.reset\",\"pcr\":");
    demo_put_u32(DEMO_SEAL_PCR);
    demo_put(",\"digest\":\"");
    demo_put_hexbuf(digest, (uint32_t)digestSz);
    demo_put("\"}\r\n");

    rc = wolfTPM2_GetKeyTemplate_RSA_SRK(&tpl);
    if (rc == TPM_RC_SUCCESS)
        rc = wolfTPM2_CreatePrimaryKey(&dev, &primary, TPM_RH_OWNER, &tpl,
                NULL, 0);
    if (rc != TPM_RC_SUCCESS) {
        demo_fail("CreatePrimaryKey", rc);
        return;
    }

    pcrArray[0] = (byte)DEMO_SEAL_PCR;
    rc = wolfTPM2_GetKeyTemplate_KeySeal(&tpl, TPM_ALG_SHA256);
    if (rc == TPM_RC_SUCCESS)
        rc = wolfTPM2_CreateKeySeal_ex(&dev, &seal, &primary.handle, &tpl,
                NULL, 0, TPM_ALG_SHA256, pcrArray, 1,
                (const byte *)seal_secret, (int)sizeof(seal_secret) - 1);
    if (rc != TPM_RC_SUCCESS) {
        demo_fail("CreateKeySeal", rc);
        goto out_primary;
    }
    demo_put("{\"event\":\"seal.create\",\"pub_bytes\":");
    demo_put_u32(seal.pub.size);
    demo_put(",\"priv_bytes\":");
    demo_put_u32(seal.priv.size);
    demo_put(",\"ok\":true}\r\n");

    /* Unseal while the measurement still matches. */
    outSz = (int)sizeof(out);
    memset(out, 0, sizeof(out));
    rc = try_unseal(&primary, &seal, pcrArray, out, &outSz);
    demo_put("{\"event\":\"seal.unseal\",\"stage\":\"before\",\"ok\":");
    demo_put(rc == TPM_RC_SUCCESS ? "true" : "false");
    if (rc == TPM_RC_SUCCESS) {
        demo_put(",\"secret\":\"");
        demo_put((const char *)out);
        demo_put("\"");
    }
    demo_put("}\r\n");

    /* Change the measurement, which must make the same unseal fail. */
    rc = extend_pcr(DEMO_SEAL_PCR);
    if (rc == TPM_RC_SUCCESS)
        rc = read_pcr(DEMO_SEAL_PCR, digest, &digestSz);
    if (rc == TPM_RC_SUCCESS) {
        demo_put("{\"event\":\"seal.extend\",\"pcr\":");
        demo_put_u32(DEMO_SEAL_PCR);
        demo_put(",\"digest\":\"");
        demo_put_hexbuf(digest, (uint32_t)digestSz);
        demo_put("\"}\r\n");
    }

    outSz = (int)sizeof(out);
    memset(out, 0, sizeof(out));
    rc = try_unseal(&primary, &seal, pcrArray, out, &outSz);
    demo_put("{\"event\":\"seal.unseal\",\"stage\":\"after\",\"ok\":");
    demo_put(rc == TPM_RC_SUCCESS ? "true" : "false");
    demo_put(",\"rc\":\"0x");
    demo_put_hex32((uint32_t)rc);
    demo_put("\"}\r\n");

    /* A refusal here is the pass: the secret is bound to the measurement. */
    demo_put("{\"event\":\"seal.end\",\"pass\":");
    demo_put(rc != TPM_RC_SUCCESS ? "true" : "false");
    demo_put("}\r\n");

    (void)wolfTPM2_ResetPCR(&dev, DEMO_SEAL_PCR);

out_primary:
    wolfTPM2_UnloadHandle(&dev, &primary.handle);
}

/* The endorsement certificates live in the TCG-reserved NV range. Each one
 * is streamed out as it is read rather than collected, because several
 * kilobytes of certificate will not all fit alongside everything else. */
static void act_ek_certs(void)
{
    static byte cert[1600];
    word32 nvIndex;
    int found = 0, i, rc;
    static const word32 ekIndices[] = {
        TPM2_NV_RSA_EK_CERT, TPM2_NV_ECC_EK_CERT
    };

    demo_put("{\"event\":\"ek.begin\"}\r\n");
    for (i = 0; i < (int)(sizeof(ekIndices) / sizeof(ekIndices[0])); i++) {
        WOLFTPM2_NV nv;
        word32 sz = (word32)sizeof(cert);

        nvIndex = ekIndices[i];
        memset(&nv, 0, sizeof(nv));
        nv.handle.hndl = nvIndex;

        rc = wolfTPM2_NVReadAuth(&dev, &nv, nvIndex, cert, &sz, 0);
        if (rc != TPM_RC_SUCCESS)
            continue;

        found++;
        demo_put("{\"event\":\"ek.cert\",\"index\":\"0x");
        demo_put_hex32(nvIndex);
        demo_put("\",\"bytes\":");
        demo_put_u32(sz);
        demo_put(",\"b64\":\"");
        demo_put_b64(cert, sz);
        demo_put("\"}\r\n");
    }
    demo_put("{\"event\":\"ek.end\",\"found\":");
    demo_put_u32((uint32_t)found);
    demo_put("}\r\n");
}

void main(void)
{
    int rc;

    demo_board_init();
    demo_put("{\"event\":\"boot\",\"host\":\"psoc_c3\"}\r\n");

    rc = wolfTPM2_Init(&dev, TPM2_IoCb, NULL);
    if (rc != TPM_RC_SUCCESS) {
        demo_fail("wolfTPM2_Init", rc);
        demo_put("{\"event\":\"ready\",\"tpm\":false}\r\n");
    }
    else {
        demo_put("{\"event\":\"ready\",\"tpm\":true}\r\n");
    }

    /* One character selects an act. Anything else is ignored, so line noise
     * from a host opening the port cannot start a run. */
    for (;;) {
        switch (demo_getc()) {
            case 'i': act_identity(); break;
            case 'p': act_pcr();      break;
            case 'l': act_seal();     break;
            case 'e': act_ek_certs(); break;
            case 's': act_sign(&dev, 0); break;
            case 't': act_sign(&dev, 1); break;  /* tampered */
            default:  break;
        }
    }
}
