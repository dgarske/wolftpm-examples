# build-windows.ps1
#
# Build the gateway host driver and its smoke test on Windows with MSYS2 mingw-w64.
#
# The MSYS2 gcc must have its own bin directory on PATH or it starts far enough
# to answer --version and then fails to compile with no diagnostic at all.
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

Set-Location $PSScriptRoot
$env:PATH = 'C:\msys64\mingw64\bin;' + $env:PATH

$o = & gcc.exe -Wall -Wextra -DGW_STANDALONE_TYPES `
    -o gw_win_test.exe gw_win_test.c ..\host\tpm_io_uart_gateway.c 2>&1
Write-Output ("build exit=" + $LASTEXITCODE)
$o | ForEach-Object { Write-Output ("  " + $_) }
if (Test-Path gw_win_test.exe) {
    Write-Output "run: .\gw_win_test.exe <COMn> [baud]"
    [System.IO.Ports.SerialPort]::GetPortNames() | ForEach-Object { Write-Output ("  port: " + $_) }
}