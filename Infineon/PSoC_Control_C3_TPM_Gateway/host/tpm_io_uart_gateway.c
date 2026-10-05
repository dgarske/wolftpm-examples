/* tpm_io_uart_gateway.c
 *
 * wolfTPM IO callback over the PSOC Control C3 UART-to-I2C gateway.
 *
 * The TIS state machine stays here on the host; the gateway performs one
 * register transfer per frame. See ../fw/gateway_proto.h for the wire format,
 * which both ends share verbatim.
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
#include <stdlib.h>

#include "tpm_io_uart_gateway.h"
#include "../fw/gateway_proto.h"

#ifdef _WIN32
    #include <windows.h>
    static HANDLE gwFd = INVALID_HANDLE_VALUE;
    #define GW_VALID()  (gwFd != INVALID_HANDLE_VALUE)
#else
    #include <fcntl.h>
    #include <unistd.h>
    #include <termios.h>
    #include <errno.h>
    #include <sys/ioctl.h>
    static int gwFd = -1;
    #define GW_VALID()  (gwFd >= 0)
#endif

#ifndef GW_DEFAULT_BAUD
#define GW_DEFAULT_BAUD 115200
#endif
/* A TIS transfer is short. This only has to outlast the gateway's own I2C
 * retry budget, which is bounded. */
#ifndef GW_READ_TIMEOUT_MS
#define GW_READ_TIMEOUT_MS 2000
#endif
#ifndef GW_RETRIES
#define GW_RETRIES 3
#endif

static unsigned char crc8_update(unsigned char crc, unsigned char byte)
{
    int b;

    crc ^= byte;
    for (b = 0; b < 8; b++) {
        if ((crc & 0x80) != 0)
            crc = (unsigned char)((crc << 1) ^ GW_CRC8_POLY);
        else
            crc = (unsigned char)(crc << 1);
    }
    return crc;
}

static unsigned char crc8(const unsigned char* buf, int len)
{
    unsigned char crc = 0x00;
    int i;

    for (i = 0; i < len; i++)
        crc = crc8_update(crc, buf[i]);
    return crc;
}

#ifdef _WIN32

static int gw_open(const char* port, unsigned int baud)
{
    DCB dcb;
    COMMTIMEOUTS to;
    char path[64];

    /* Ports above COM9 need the device namespace prefix; it is harmless on
     * the low ones, so it is applied unconditionally. */
    if (port[0] == '\\')
        snprintf(path, sizeof(path), "%s", port);
    else
        snprintf(path, sizeof(path), "\\\\.\\%s", port);

    gwFd = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
            OPEN_EXISTING, 0, NULL);
    if (gwFd == INVALID_HANDLE_VALUE)
        return -1;

    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(gwFd, &dcb)) {
        CloseHandle(gwFd);
        gwFd = INVALID_HANDLE_VALUE;
        return -1;
    }
    dcb.BaudRate = baud;
    dcb.ByteSize = 8;
    dcb.Parity   = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary  = TRUE;
    /* No flow control of any kind: the gateway drives neither, and leaving
     * DTR/RTS handshaking on stalls the first write. Both modem lines are
     * left deasserted rather than driven, because many USB-serial bridges
     * wire DTR or RTS to the target's reset: asserting them on open would
     * reset the board every time the host connects. */
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl  = DTR_CONTROL_DISABLE;
    dcb.fRtsControl  = RTS_CONTROL_DISABLE;
    dcb.fOutX = FALSE;
    dcb.fInX  = FALSE;
    if (!SetCommState(gwFd, &dcb)) {
        CloseHandle(gwFd);
        gwFd = INVALID_HANDLE_VALUE;
        return -1;
    }

    memset(&to, 0, sizeof(to));
    to.ReadIntervalTimeout        = 0;
    to.ReadTotalTimeoutConstant   = GW_READ_TIMEOUT_MS;
    to.ReadTotalTimeoutMultiplier = 0;
    to.WriteTotalTimeoutConstant  = GW_READ_TIMEOUT_MS;
    SetCommTimeouts(gwFd, &to);

    PurgeComm(gwFd, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return 0;
}

static int gw_write(const unsigned char* buf, int len)
{
    DWORD wrote = 0;

    if (!WriteFile(gwFd, buf, (DWORD)len, &wrote, NULL))
        return -1;
    return ((int)wrote == len) ? 0 : -1;
}

static int gw_read(unsigned char* buf, int len)
{
    DWORD got = 0;
    int off = 0;

    while (off < len) {
        if (!ReadFile(gwFd, buf + off, (DWORD)(len - off), &got, NULL))
            return -1;
        if (got == 0)
            return -1;      /* timed out */
        off += (int)got;
    }
    return 0;
}

static void gw_flush(void)
{
    if (GW_VALID())
        PurgeComm(gwFd, PURGE_RXCLEAR);
}

void TPM2_UartGateway_Close(void)
{
    if (GW_VALID()) {
        CloseHandle(gwFd);
        gwFd = INVALID_HANDLE_VALUE;
    }
}

#else /* POSIX */

static int gw_open(const char* port, unsigned int baud)
{
    struct termios tio;
    speed_t sp;
    int mbits;

    gwFd = open(port, O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (gwFd < 0)
        return -1;

    if (tcgetattr(gwFd, &tio) != 0) {
        close(gwFd);
        gwFd = -1;
        return -1;
    }
    cfmakeraw(&tio);
    switch (baud) {
        case 9600:    sp = B9600;    break;
        case 19200:   sp = B19200;   break;
        case 38400:   sp = B38400;   break;
        case 57600:   sp = B57600;   break;
        case 230400:  sp = B230400;  break;
        case 460800:  sp = B460800;  break;
        case 921600:  sp = B921600;  break;
        case 1000000: sp = B1000000; break;
        default:      sp = B115200;  break;
    }
    cfsetispeed(&tio, sp);
    cfsetospeed(&tio, sp);
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
    /* Block until at least one byte or the decisecond timer expires; the
     * read loop below reassembles a frame across several of these. */
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = (cc_t)((GW_READ_TIMEOUT_MS + 99) / 100);
    /* Do not hang up on close, so the line is not dropped under a board that
     * treats it as reset. */
    tio.c_cflag &= ~HUPCL;
    if (tcsetattr(gwFd, TCSANOW, &tio) != 0) {
        close(gwFd);
        gwFd = -1;
        return -1;
    }
    /* Deassert DTR and RTS for the same reason as the Windows path. */
    mbits = TIOCM_DTR | TIOCM_RTS;
    (void)ioctl(gwFd, TIOCMBIC, &mbits);
    tcflush(gwFd, TCIOFLUSH);
    return 0;
}

static int gw_write(const unsigned char* buf, int len)
{
    int off = 0;
    ssize_t n;

    while (off < len) {
        n = write(gwFd, buf + off, (size_t)(len - off));
        if (n <= 0) {
            if ((n < 0) && (errno == EINTR))
                continue;
            return -1;
        }
        off += (int)n;
    }
    return 0;
}

static int gw_read(unsigned char* buf, int len)
{
    int off = 0;
    ssize_t n;

    while (off < len) {
        n = read(gwFd, buf + off, (size_t)(len - off));
        if (n <= 0) {
            if ((n < 0) && (errno == EINTR))
                continue;
            return -1;      /* timed out or closed */
        }
        off += (int)n;
    }
    return 0;
}

static void gw_flush(void)
{
    if (GW_VALID())
        tcflush(gwFd, TCIFLUSH);
}

void TPM2_UartGateway_Close(void)
{
    if (GW_VALID()) {
        close(gwFd);
        gwFd = -1;
    }
}

#endif /* _WIN32 */

int TPM2_UartGateway_Open(const char* port, unsigned int baud)
{
    if (GW_VALID())
        return 0;
    if (port == NULL)
        port = getenv("WOLFTPM_GW_PORT");
    if (port == NULL) {
#ifdef _WIN32
        port = "COM3";
#else
        port = "/dev/ttyACM0";
#endif
    }
    if (baud == 0)
        baud = GW_DEFAULT_BAUD;
    return gw_open(port, baud);
}

/* One request, one response. The caller retries; this does not, so that a
 * resynchronising flush happens between attempts rather than inside one. */
static int gw_txn(unsigned char op, unsigned char reg, unsigned char* data,
    int len, int maxRsp, unsigned char* status, int* rspOut)
{
    unsigned char req[GW_HDR_LEN + GW_MAX_PAYLOAD + 1];
    unsigned char rsp[GW_HDR_LEN];
    unsigned char crc;
    int reqLen;
    int rspLen;
    int i;

    if ((len < 0) || (len > GW_MAX_PAYLOAD))
        return -1;

    req[0] = GW_SOF_REQ;
    req[1] = op;
    req[2] = reg;
    req[3] = (unsigned char)((len >> 8) & 0xFF);
    req[4] = (unsigned char)(len & 0xFF);
    reqLen = GW_HDR_LEN;
    /* A read states the length it wants in the header and sends no payload. */
    if (op == GW_OP_WRITE) {
        for (i = 0; i < len; i++)
            req[GW_HDR_LEN + i] = data[i];
        reqLen += len;
    }
    req[reqLen] = crc8(&req[1], reqLen - 1);
    reqLen++;

    if (gw_write(req, reqLen) != 0)
        return -1;

    if (gw_read(rsp, GW_HDR_LEN) != 0)
        return -1;
    if (rsp[0] != GW_SOF_RSP)
        return -1;
    rspLen = ((int)rsp[3] << 8) | (int)rsp[4];
    if (rspLen > GW_MAX_PAYLOAD)
        return -1;

    crc = crc8(&rsp[1], GW_HDR_LEN - 1);
    if (rspLen > 0) {
        /* Read straight into the caller's buffer. A read must come back the
         * length it asked for; a ping returns an identifier whose length is
         * the gateway's to choose, bounded by the buffer. */
        if (rspLen > maxRsp)
            return -1;
        if ((op == GW_OP_READ) && (rspLen != len))
            return -1;
        if (gw_read(data, rspLen) != 0)
            return -1;
        for (i = 0; i < rspLen; i++)
            crc = crc8_update(crc, data[i]);
    }
    if (gw_read(&req[0], 1) != 0)
        return -1;
    if (req[0] != crc)
        return -1;

    *status = rsp[1];
    if (rspOut != NULL)
        *rspOut = rspLen;
    return 0;
}

static int gw_txn_retry(unsigned char op, unsigned char reg,
    unsigned char* data, int len)
{
    unsigned char status = GW_ST_BUS;
    int tries = GW_RETRIES;
    int rc = -1;

    while (tries-- > 0) {
        rc = gw_txn(op, reg, data, len, len, &status, NULL);
        if (rc == 0)
            break;
        /* A framing failure leaves unread bytes behind; drop them so the
         * next attempt starts on a clean stream. */
        gw_flush();
    }
    if (rc != 0)
        return TPM_RC_FAILURE;
    return (status == GW_ST_OK) ? TPM_RC_SUCCESS : TPM_RC_FAILURE;
}

int TPM2_UartGateway_Ping(char* idOut, int idOutSz)
{
    unsigned char buf[GW_MAX_PAYLOAD];
    unsigned char status = GW_ST_BUS;
    int rspLen = 0;
    int rc;
    int n;

    if (!GW_VALID())
        return -1;
    memset(buf, 0, sizeof(buf));
    rc = gw_txn(GW_OP_PING, 0, buf, 0, (int)sizeof(buf), &status, &rspLen);
    if (rc != 0) {
        gw_flush();
        return -1;
    }
    if ((idOut != NULL) && (idOutSz > 0)) {
        /* The identifier is not NUL terminated on the wire. */
        n = rspLen;
        if (n > idOutSz - 1)
            n = idOutSz - 1;
        memcpy(idOut, buf, (size_t)n);
        idOut[n] = '\0';
    }
    return (status == GW_ST_OK) ? 0 : -1;
}

int TPM2_IoCb_UartGateway(TPM2_CTX* ctx, int isRead, word32 addr, byte* buf,
    word16 size, void* userCtx)
{
    (void)ctx;
    (void)userCtx;

    if (!GW_VALID()) {
        if (TPM2_UartGateway_Open(NULL, 0) != 0)
            return TPM_RC_FAILURE;
    }
    if (size > GW_MAX_PAYLOAD)
        return BAD_FUNC_ARG;

    return gw_txn_retry(isRead ? GW_OP_READ : GW_OP_WRITE,
            (unsigned char)(addr & 0xFF), (unsigned char*)buf, (int)size);
}
