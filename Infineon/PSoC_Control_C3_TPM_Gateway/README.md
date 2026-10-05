# PSOC Control C3 UART-to-I2C TPM Gateway

Drive a TPM 2.0 from a PC that has no I2C of its own, by making a PSOC Control C3 the bus adapter.

The MCU runs one small application: it turns each framed request arriving on its USB serial port into one TIS register transfer on I2C and frames the result back. The TPM stack runs entirely on the PC, so the TIS state machine, the policies and the key handling all stay in wolfTPM where they already work. wolfBoot verifies and boots the bridge, so the adapter itself is measured and signature-checked before it runs.

Nothing in wolfTPM is modified.

## Tested targets

| Board | Setup | Status | Date |
|---|---|---|---|
| PSOC Control C3 (C3M6) EVK | Infineon TPM 2.0 on the mikroBUS socket, I2C | Identity, PCR read, EK certificates, seal/unseal with PCR policy, and Hash-ML-DSA sign with host-side verification, all over the gateway | 2026-09-30 |

Windows: the host driver compiles clean and links with mingw-w64, and fails an absent port correctly. Serial I/O on Windows is not yet exercised against hardware.

## Prerequisites

- `arm-none-eabi-gcc` for the MCU firmware
- SEGGER J-Link software
- wolfBoot with PSOC Control C3 support (https://github.com/wolfSSL/wolfBoot pull 915, or `master` once merged)
- wolfSSL and wolfTPM, built as below

## Build and flash the bridge

Build wolfBoot as the bootloader only. This also generates the signing key and `include/target.h`, both of which the application build uses.

```
cd <wolfboot>
cp config/examples/psoc_c3.config .config
make wolfboot.elf wolfboot.bin WOLFTPM=1 WOLFBOOT_TPM_I2C=1
```

`WOLFBOOT_TPM_I2C=1` matters for the bootloader even though the application does its own TPM work. The TPM latches its bus from the interface-select strap while its own reset is low, roughly a millisecond after the CPU starts, which is long before a verified application can run. `hal_init()` is the only thing early enough to drive it.

Then build and sign the gateway application. It compiles against wolfBoot's HAL and I2C driver and is signed with the key wolfBoot just generated:

```
cd fw
make WOLFBOOT_DIR=<wolfboot>
```

For a faster link, which lowers the per-transfer cost, add:

```
make WOLFBOOT_DIR=<wolfboot> CFLAGS="-DPSOC_C3_UART_BAUD=1000000 -DPSOC_C3_I2C_HZ=400000"
```

Assemble the two pieces into one programmable image, or flash them separately at `0x12000000` and `0x12010000`:

```
<wolfboot>/tools/bin-assemble/bin-assemble factory.bin \
    0x0     <wolfboot>/wolfboot.bin \
    0x10000 gateway_v1_signed.bin
```

Flash with J-Link. Note the programming address: this family exposes the same flash through several addresses and only this one accepts debugger writes. Do not use `-device Cortex-M33`; the generic entry cannot enumerate this part's access ports.

```
JLinkExe -device PSC3xxF -if SWD -speed 4000 -autoconnect 1
loadbin factory.bin 0x22000000
r
go
q
```

## Build the host side

```
git clone https://github.com/wolfSSL/wolfssl.git && cd wolfssl
./autogen.sh
./configure --prefix=$HOME/wolfssl-pqc --enable-wolftpm --enable-mldsa \
    --enable-mlkem --enable-experimental CFLAGS="-DWC_RSA_NO_PADDING"
make && make install

git clone https://github.com/wolfSSL/wolfTPM.git && cd wolfTPM
./autogen.sh
./configure --enable-i2c --enable-advio --enable-v185 --disable-autodetect \
    --enable-static --disable-shared --with-wolfcrypt=$HOME/wolfssl-pqc
make
```

`--enable-i2c` is what selects the TCG PTP I2C register map, which is the map the gateway speaks. `--disable-autodetect` stops wolfTPM probing the Linux kernel TPM driver first, which otherwise prints a permissions complaint before falling through. `--with-wolfcrypt` takes the install prefix, not a source directory; wolfTPM includes that prefix's `wolfssl/options.h`, so settings chosen when wolfSSL was configured, `WC_RSA_NO_PADDING` among them, do not need repeating here.

Then build the examples against the gateway:

```
make WOLFTPM_DIR=<wolftpm> WOLFSSL_DIR=$HOME/wolfssl-pqc
```

## Run

```
export WOLFTPM_GW_PORT=/dev/serial/by-id/usb-SEGGER_J-Link_000<serial>-if00
export WOLFTPM_GW_BAUD=1000000
export LD_LIBRARY_PATH=$HOME/wolfssl-pqc/lib
./bin/caps
```

Use the by-id path rather than `/dev/ttyACM<n>`: the number changes whenever the board re-enumerates, and a stale one looks exactly like a dead gateway. On Windows the port is a `COM<n>` name.

The baud must match what the firmware was built with. A mismatch produces silence, not an error.

## How wolfTPM reaches the gateway without being modified

Two ways, and the example shows both.

An application that calls `wolfTPM2_Init(&dev, TPM2_IoCb_UartGateway, NULL)` simply passes its own callback, which wolfTPM has always supported. `test/gw_testresult.c` does this.

Reusing wolfTPM's own examples needs something else, because they call the built-in `TPM2_IoCb`. wolfTPM defines that symbol in one object of its library, and that object defines nothing else a normal program needs. Linking against the **static** library with `host/gw_tpm_iocb.c` present therefore satisfies the reference locally and the library's HAL object is never pulled in. That is why the build above asks for `--enable-static --disable-shared`. The Makefile builds wolfTPM's unmodified example sources this way.

## Testing without hardware

`test/gw_model.py` implements the gateway protocol on a pseudo-terminal, backed by a dictionary of registers. That lets the host driver be exercised for framing, CRC, oversize rejection and recovery from corrupted responses with no board attached, which makes the host side testable in CI.

```
python3 test/gw_model.py        # prints the pty name
./gw_test <pty>                 # framing and bounds
./gw_test <pty> resync          # recovery from corrupted responses
```

## The I2C sequence is not the obvious one

The firmware addresses the register, releases the bus, waits, then reads, rather than holding the bus for a repeated start. A TPM typically NAKs for tens of microseconds after being addressed while it wakes, and the specification asks for a guard time between transfers, which a repeated start cannot provide. The sequence is taken from wolfBoot's `src/tpm.c`, where it was established on silicon. Do not simplify it back to a repeated start.

The interface-select strap matters just as much. A TPM offering both SPI and I2C latches its choice from that pin while its own reset is low, and left to its internal pull-up it selects SPI. wolfBoot drives it before anything else, so a gateway built on top of wolfBoot inherits that; a standalone build must do the same or the part comes up on the wrong bus and answers nothing.

## What the link costs

Each register transfer is one round trip over USB serial: 4.0 ms at 115200 baud and 2.0 ms at 1000000. Those figures are the USB polling interval rather than line time, so raising the baud past 1000000 does not help and the I2C bus rate is not on the critical path.

End-to-end timing is dominated by the TPM, not the link. ML-DSA signing uses rejection sampling, so the same operation measured between about one and two and a half seconds on the same part across consecutive runs. Budget for the slow case.

## Troubleshooting

**Nothing on the serial port.** Check the port name and that the baud matches the firmware.

**The TPM answers registers but every command fails with `TPM_RC_FAILURE` (0x101).** Run `test/gw_testresult.c`. If `TPM2_Init_ex` succeeds while `TPM2_Startup` fails, the whole path to the part is good and the part itself is in a failure state. A reset, including a debug-probe pin reset, does not always clear it; removing power does. Note that disabling the USB port in software only removes power if the hub supports per-port power switching. Check before relying on it, because a hub reporting **ganged** switching cannot cut one port's VBUS and the board stays powered:

```
lsusb -v -s <bus>:<hubdev> | grep -i "power switching"
```
