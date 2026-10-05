/* gateway.c
 *
 * UART-to-I2C gateway for a TPM 2.0 on the PSOC Control C3 (C3M6).
 *
 * The host runs the TPM stack and owns the TIS state machine. This firmware
 * owns nothing but the bus: it turns each framed request into one TIS-over-I2C
 * register transfer and frames the result back. Keeping the state machine on
 * the host is what lets the same gateway serve any host language.
 *
 * The I2C sequence is lifted from wolfBoot's src/tpm.c rather than rewritten,
 * because the no-repeated-start form it uses is a property of this part and
 * was established on silicon.
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
#include "hal/psoc_c3.h"
#include "i2c_drv.h"

#include "gateway_proto.h"

extern void uart_init(void);
extern void uart_write(const char *buf, unsigned int sz);

/* The console SCB's base is private to the wolfBoot HAL, so derive it here
 * from the same build option rather than exporting it. */
#ifndef PSOC_C3_UART_SCB
#define PSOC_C3_UART_SCB    3
#endif
#define GW_UART_SCB_BASE    PSOC_C3_SCB_BASE(PSOC_C3_UART_SCB)

/* Where the TPM's interface-select strap lands on the evaluation kit. */
#ifndef PSOC_C3_TPM_SEL_PORT
#define PSOC_C3_TPM_SEL_PORT    7
#endif
#ifndef PSOC_C3_TPM_SEL_PIN
#define PSOC_C3_TPM_SEL_PIN     7
#endif

/* Waiting for a request is open ended, but once a frame has started its
 * remaining bytes must arrive or the parser is stuck holding a half frame.
 * Bounding only the continuation lets a truncated frame fall out and the next
 * start of frame resynchronise. */
#ifndef GW_FRAME_TIMEOUT
#define GW_FRAME_TIMEOUT    2000000UL
#endif

#ifndef TPM2_I2C_ADDR
#define TPM2_I2C_ADDR       0x2E    /* 7-bit TCG TIS address */
#endif
#ifndef TPM_I2C_TRIES
#define TPM_I2C_TRIES       10
#endif
/* The part NAKs for roughly 80us after being addressed while it wakes, and
 * the specification asks for a guard time between transfers. A bounded spin
 * is enough; the retry loop covers the rest. */
#ifndef TPM_I2C_GUARD_LOOPS
#define TPM_I2C_GUARD_LOOPS 20000
#endif

static uint8_t payload[GW_MAX_PAYLOAD];
/* One extra byte: the register precedes the payload in a single write. */
static uint8_t i2cbuf[GW_MAX_PAYLOAD + 1];
static uint8_t frame[GW_HDR_LEN + GW_MAX_PAYLOAD + 1];

static void tpm_i2c_guard(void)
{
    volatile uint32_t i;

    for (i = 0; i < (uint32_t)TPM_I2C_GUARD_LOOPS; i++)
        ;
}

/* Address the register and release the bus rather than holding it for a
 * repeated start: this part needs the guard time between the two transfers,
 * which a repeated start cannot provide. */
static int tpm_i2c_read(uint8_t reg, uint8_t *data, uint32_t len)
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

static int tpm_i2c_write(uint8_t reg, const uint8_t *data, uint32_t len)
{
    int tries = TPM_I2C_TRIES;
    uint32_t i;
    int ret;

    i2cbuf[0] = reg;
    for (i = 0; i < len; i++)
        i2cbuf[i + 1] = data[i];

    do {
        ret = i2c_write(TPM2_I2C_ADDR, i2cbuf, len + 1, 1);
        if (ret != I2C_OK)
            tpm_i2c_guard();
    } while ((ret != I2C_OK) && (--tries > 0));

    /* A command payload can carry an authValue; do not leave it in the
     * staging buffer between transfers. */
    for (i = 0; i < len + 1; i++)
        i2cbuf[i] = 0;

    return ret;
}

static uint8_t status_of(int i2c_ret)
{
    uint8_t st;

    switch (i2c_ret) {
        case I2C_OK:
            st = GW_ST_OK;
            break;
        case I2C_ERR_NACK:
            st = GW_ST_NACK;
            break;
        case I2C_ERR_TIMEOUT:
            st = GW_ST_TIMEOUT;
            break;
        case I2C_ERR_ARG:
            st = GW_ST_BADLEN;
            break;
        default:
            st = GW_ST_BUS;
            break;
    }
    return st;
}

static uint8_t crc8_update(uint8_t crc, uint8_t byte)
{
    int b;

    crc ^= byte;
    for (b = 0; b < 8; b++) {
        if ((crc & 0x80) != 0)
            crc = (uint8_t)((crc << 1) ^ GW_CRC8_POLY);
        else
            crc = (uint8_t)(crc << 1);
    }
    return crc;
}

static uint8_t crc8(const uint8_t *buf, uint32_t len)
{
    uint8_t crc = 0x00;
    uint32_t i;

    for (i = 0; i < len; i++)
        crc = crc8_update(crc, buf[i]);
    return crc;
}

static uint8_t uart_getc(void)
{
    while ((SCB_RX_FIFO_STATUS(GW_UART_SCB_BASE) &
            SCB_RX_FIFO_USED_Msk) == 0)
        ;
    return (uint8_t)SCB_RX_FIFO_RD(GW_UART_SCB_BASE);
}

/* Returns 0 on a byte, -1 if none arrived within the frame timeout. */
static int uart_getc_timed(uint8_t *out)
{
    uint32_t timeout = GW_FRAME_TIMEOUT;

    while (((SCB_RX_FIFO_STATUS(GW_UART_SCB_BASE) &
             SCB_RX_FIFO_USED_Msk) == 0) && (timeout > 0))
        timeout--;
    if (timeout == 0)
        return -1;
    *out = (uint8_t)SCB_RX_FIFO_RD(GW_UART_SCB_BASE);
    return 0;
}

/* Built in place so the whole frame leaves as one write: a partially written
 * frame is what a resynchronising host has to unpick, so it is worth
 * avoiding. */
static void send_response(uint8_t status, const uint8_t *data, uint32_t len)
{
    uint32_t i;

    frame[0] = GW_SOF_RSP;
    frame[1] = status;
    frame[2] = 0;
    frame[3] = (uint8_t)((len >> 8) & 0xFF);
    frame[4] = (uint8_t)(len & 0xFF);
    for (i = 0; i < len; i++)
        frame[GW_HDR_LEN + i] = data[i];
    frame[GW_HDR_LEN + len] = crc8(&frame[1], (GW_HDR_LEN - 1) + len);

    uart_write((const char *)frame, (unsigned int)(GW_HDR_LEN + len + 1));
}

static void do_read(uint8_t reg, uint32_t len)
{
    int ret;

    if ((len == 0) || (len > GW_MAX_PAYLOAD)) {
        send_response(GW_ST_BADLEN, 0, 0);
        return;
    }
    ret = tpm_i2c_read(reg, payload, len);
    if (ret != I2C_OK) {
        send_response(status_of(ret), 0, 0);
        return;
    }
    send_response(GW_ST_OK, payload, len);
}

static void do_write(uint8_t reg, uint32_t len)
{
    int ret;

    ret = tpm_i2c_write(reg, payload, len);
    send_response(status_of(ret), 0, 0);
}

/* Ping doubles as a presence check: reading the TIS access register is free
 * of side effects, so a host can tell an absent or mis-strapped part from a
 * gateway that is merely alive. Over I2C that register is at 0x04, not the
 * 0x00 the SPI register map uses. */
static void do_ping(void)
{
    static const char id[] = GW_PING_ID;
    uint8_t probe;
    uint32_t n = 0;
    int ret;

    while (id[n] != 0)
        n++;
    ret = tpm_i2c_read(GW_TIS_ACCESS_REG, &probe, 1);
    send_response((ret == I2C_OK) ? GW_ST_OK : status_of(ret),
            (const uint8_t *)id, n);
}

/* Read one request. Bytes before the start of frame are discarded, which is
 * how a host recovers from a desynchronised stream: it pauses, then sends a
 * fresh frame. */
static void handle_one(void)
{
    uint8_t op;
    uint8_t reg;
    uint8_t lo;
    uint8_t hi;
    uint8_t crc;
    uint32_t len;
    uint32_t i;

    while (uart_getc() != GW_SOF_REQ)
        ;

    if ((uart_getc_timed(&op) != 0) || (uart_getc_timed(&reg) != 0) ||
            (uart_getc_timed(&hi) != 0) || (uart_getc_timed(&lo) != 0))
        return;
    len = ((uint32_t)hi << 8) | (uint32_t)lo;

    /* A read states the length it wants and sends no payload; only a write
     * puts bytes on the wire after the header. */
    if ((op == GW_OP_WRITE) && (len > GW_MAX_PAYLOAD)) {
        /* The length cannot be trusted, so the payload cannot be drained.
         * Answer and let the host resynchronise on the next frame. */
        send_response(GW_ST_BADLEN, 0, 0);
        return;
    }

    crc = crc8_update(0x00, op);
    crc = crc8_update(crc, reg);
    crc = crc8_update(crc, hi);
    crc = crc8_update(crc, lo);
    if (op == GW_OP_WRITE) {
        for (i = 0; i < len; i++) {
            if (uart_getc_timed(&payload[i]) != 0)
                return;
            crc = crc8_update(crc, payload[i]);
        }
    }
    if (uart_getc_timed(&lo) != 0)
        return;
    if (lo != crc) {
        send_response(GW_ST_BADCRC, 0, 0);
        return;
    }

    switch (op) {
        case GW_OP_READ:
            do_read(reg, len);
            break;
        case GW_OP_WRITE:
            do_write(reg, len);
            break;
        case GW_OP_PING:
            do_ping();
            break;
        default:
            send_response(GW_ST_BADOP, 0, 0);
            break;
    }
}

/* The TPM latches which bus it speaks from this strap while its own reset is
 * low, and its reset trails the CPU's by about a millisecond, so the pin has
 * to be driven before anything else runs. */
static void tpm_iface_select(void)
{
    psoc_c3_peri_init();
    psoc_c3_pin_setup(PSOC_C3_TPM_SEL_PORT, PSOC_C3_TPM_SEL_PIN, 0,
            GPIO_CFG_DM_STRONG);
    GPIO_PRT_OUT(PSOC_C3_TPM_SEL_PORT) &= ~(1UL << PSOC_C3_TPM_SEL_PIN);
}

void main(void)
{
    tpm_iface_select();
    uart_init();
    i2c_init();

    while (1)
        handle_one();
}
