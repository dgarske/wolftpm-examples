/* gateway_proto.h
 *
 * Wire format shared by the C3M6 gateway firmware and its host driver.
 *
 * One request is one TIS register transfer. The host owns the TIS state
 * machine; the gateway owns only the bus. Both frames use the same five byte
 * header so a desynchronised stream is unpicked the same way in both
 * directions.
 *
 *   offset 0   start of frame, 0xA5 request / 0x5A response
 *   offset 1   operation (request) or status (response)
 *   offset 2   TIS register (request) or reserved, 0 (response)
 *   offset 3   length, high byte
 *   offset 4   length, low byte
 *   offset 5   payload, length bytes
 *   last       CRC-8 over offsets 1 to 4 and the payload
 *
 * On a read the request length is the number of bytes wanted and carries no
 * payload; the response carries them. On a write the request carries the
 * payload and the response length is zero.
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
#ifndef GATEWAY_PROTO_H_INCLUDED
#define GATEWAY_PROTO_H_INCLUDED

#define GW_SOF_REQ      0xA5
#define GW_SOF_RSP      0x5A
#define GW_HDR_LEN      5

/* Matches wolfTPM's MAX_SPI_FRAMESIZE: the TIS layer never asks for more in
 * one transfer, so the gateway never needs a larger buffer. */
#define GW_MAX_PAYLOAD  64

#define GW_OP_READ      0x52    /* 'R' */
#define GW_OP_WRITE     0x57    /* 'W' */
#define GW_OP_PING      0x50    /* 'P' */

#define GW_ST_OK        0x00
#define GW_ST_NACK      0x01
#define GW_ST_TIMEOUT   0x02
#define GW_ST_BUS       0x03
#define GW_ST_BADCRC    0x04
#define GW_ST_BADLEN    0x05
#define GW_ST_BADOP     0x06

#define GW_CRC8_POLY    0x07
#define GW_PING_ID      "PSOC-C3-TPM-GW/1"

/* TIS access register in the I2C register map, used by the ping probe. */
#define GW_TIS_ACCESS_REG   0x04

#endif /* GATEWAY_PROTO_H_INCLUDED */
