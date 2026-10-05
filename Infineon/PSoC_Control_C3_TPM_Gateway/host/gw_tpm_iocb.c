/* gw_tpm_iocb.c
 *
 * Provides TPM2_IoCb so the gateway replaces wolfTPM's built-in HAL at link
 * time, with no change to wolfTPM itself.
 *
 * wolfTPM defines TPM2_IoCb in one object of its library, and that object
 * defines nothing else that a normal program needs. Linking against the
 * static library with this file present therefore satisfies the reference
 * here and the library's HAL is never pulled in. Stock wolfTPM examples then
 * reach the TPM through the gateway unmodified.
 *
 * A program that calls wolfTPM2_Init() with its own callback does not need
 * this file; it is only for reusing code that expects the built-in one.
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
#include <wolftpm/tpm2.h>

#include "tpm_io_uart_gateway.h"

int TPM2_IoCb(TPM2_CTX* ctx, INT32 isRead, UINT32 addr, BYTE* buf,
    UINT16 size, void* userCtx)
{
    return TPM2_IoCb_UartGateway(ctx, (int)isRead, (word32)addr, buf,
        (word16)size, userCtx);
}
