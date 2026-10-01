#!/usr/bin/env python3
# gw_model.py
#
# Copyright (C) 2006-2026 wolfSSL Inc.
#
# This file is part of wolfTPM.
#
# wolfTPM is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# wolfTPM is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA

"""Gateway model on a PTY, for testing the host driver without hardware.

Implements gateway_proto.h exactly. Backs the TIS registers with a dict so a
read returns what a write put there, which is enough to prove the host's
framing, CRC span and resynchronisation are right.
"""
import os, pty, sys, threading

SOF_REQ, SOF_RSP, HDR = 0xA5, 0x5A, 5
OP_READ, OP_WRITE, OP_PING = 0x52, 0x57, 0x50
ST_OK, ST_BADCRC, ST_BADLEN, ST_BADOP = 0x00, 0x04, 0x05, 0x06
MAX_PAYLOAD, POLY = 64, 0x07
PING_ID = b"PSOC-C3-TPM-GW/1"


def crc8(data):
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ POLY) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


class Gateway(threading.Thread):
    def __init__(self, fd, corrupt_first=0):
        super().__init__(daemon=True)
        self.fd = fd
        self.regs = {}
        self.corrupt_first = corrupt_first
        self.served = 0

    def _rd(self, n):
        out = b""
        while len(out) < n:
            c = os.read(self.fd, n - len(out))
            if not c:
                raise EOFError
            out += c
        return out

    def _respond(self, status, payload=b""):
        body = bytes([status, 0, len(payload) >> 8, len(payload) & 0xFF]) + payload
        frame = bytes([SOF_RSP]) + body + bytes([crc8(body)])
        # Deliberately damage the first N responses to exercise host resync.
        if self.corrupt_first > 0:
            self.corrupt_first -= 1
            frame = frame[:-1] + bytes([frame[-1] ^ 0xFF])
        os.write(self.fd, frame)

    def run(self):
        try:
            while True:
                while self._rd(1)[0] != SOF_REQ:
                    pass
                op, reg, hi, lo = self._rd(4)
                ln = (hi << 8) | lo
                if op == OP_WRITE and ln > MAX_PAYLOAD:
                    self._respond(ST_BADLEN)
                    continue
                body = bytes([op, reg, hi, lo])
                payload = self._rd(ln) if op == OP_WRITE else b""
                crc = self._rd(1)[0]
                if crc != crc8(body + payload):
                    self._respond(ST_BADCRC)
                    continue
                self.served += 1
                if op == OP_PING:
                    self._respond(ST_OK, PING_ID)
                elif op == OP_WRITE:
                    self.regs[reg] = payload
                    self._respond(ST_OK)
                elif op == OP_READ:
                    if ln == 0 or ln > MAX_PAYLOAD:
                        self._respond(ST_BADLEN)
                    else:
                        have = self.regs.get(reg, b"")
                        self._respond(ST_OK, (have + bytes(ln))[:ln])
                else:
                    self._respond(ST_BADOP)
        except (EOFError, OSError):
            pass


def spawn(corrupt_first=0):
    master, slave = pty.openpty()
    gw = Gateway(master, corrupt_first)
    gw.start()
    return gw, os.ttyname(slave), slave


if __name__ == "__main__":
    gw, name, _ = spawn(int(sys.argv[1]) if len(sys.argv) > 1 else 0)
    print(name, flush=True)
    gw.join()