# PSOC Control C3 on-board demo

Work towards running the post-quantum TPM demo on the MCU itself rather than on a host, with the host reduced to a screen. See the gateway example in this repository for the host-driven arrangement.

## ML-DSA verification benchmark

`mldsa_bench.c` times an ML-DSA-87 signature verification on the board. It is the measurement that decides whether the demo can move: a TPM-made post-quantum signature is only worth verifying locally if the MCU can do it inside a demo beat. No TPM is involved, so the board needs nothing attached; the signature is a fixed vector in `mldsa_tv.h`, generated once on a host.

### Result

Measured on a PSOC Control C3 (C3M6), 1 October 2026:

```
  timebase     DWT cycle counter
  public key   2592 bytes
  signature    4627 bytes
  key object   9112 bytes
  run 1: 7476351 cycles, 41 ms at 180 MHz
  run 2: 7472930 cycles, 41 ms at 180 MHz
  run 3: 7472930 cycles, 41 ms at 180 MHz
  run 4: 7472930 cycles, 41 ms at 180 MHz
  run 5: 7472930 cycles, 41 ms at 180 MHz
  best 7472930 worst 7476351 mean 7473614 cycles
  mean 41 ms
PASS
```

**7.47 million cycles, or 41 ms**, repeatable to within 0.05 percent once the first run has warmed the flash cache. That is fast enough that the verification is not the slow part of a demo beat; the TPM's own signing takes between one and two and a half seconds.

### Footprint

```
   text   23896    data 1348    bss 9452
```

Roughly 25 KB of flash, of which about 7 KB is the embedded test vector, and 10.8 KB of RAM. Against the 512 KB flash and 128 KB SRAM on this part, verification is not a constraint.

The setting that makes this true is `WOLFSSL_MLDSA_VERIFY_SMALLEST_MEM` in `user_settings.h`. Without it, verifying parameter set 87 expands the matrix A into about 80 KB of heap, measured with a counting allocator around a real verification. With it the matrix is recomputed as needed, the remaining working buffers live inside the key object, and verification allocates nothing at all. That is a build option rather than a code change, and it is the difference between fitting easily and not fitting.

### Building

The benchmark reuses wolfBoot's HAL for the console and its startup code, and is built and signed the same way as the gateway example:

```
make WOLFBOOT_DIR=<wolfboot> WOLFSSL_DIR=<wolfssl>
```

For a timing measurement alone it can also be linked to run directly from the boot address with no bootloader involved, which is how the figures above were taken:

```
make mldsa_bench.bin WOLFBOOT_DIR=<wolfboot> WOLFSSL_DIR=<wolfssl> \
     APP_ADDR=0x12000000 APP_SIZE=0x20000
```

Then flash `mldsa_bench.bin` at `0x22000000` with J-Link and read the console at 115200.

### Timebase and core clock

The benchmark prefers the trace unit's cycle counter and falls back to SysTick, reporting which it used. Not every Cortex-M33 implements the trace unit and TrustZone can put it out of reach, so a figure taken from a counter that was never running would otherwise look like a real measurement. On this part the trace unit works.

The core clock is measured rather than assumed. `clock_calibrate()` spins for a known number of cycles between two console markers, and a host timing those markers derives the clock:

```
  100000006 cycles in 0.5550 s -> 180.175 MHz
  100000006 cycles in 0.5551 s -> 180.158 MHz
  100000019 cycles in 0.5550 s -> 180.175 MHz
  100000019 cycles in 0.5550 s -> 180.192 MHz
```

**180.17 MHz**, consistent to 0.02 percent across four rounds. Worth stating plainly because it is easy to get wrong: this is the core clock, and it is not the 48 MHz peripheral clock that the console baud divider is derived from. Assuming the peripheral rate applied to the core would overstate every timing on this part by a factor of nearly four.

## Driving the TPM from the board

`tpm_identity.c` reads the TPM's identity with no host involved. It needs no changes to wolfTPM: `WOLFTPM_EXAMPLE_HAL` declares `TPM2_IoCb` without compiling any of wolfTPM's own HAL sources, and `tpm_io_i2c_c3.c` supplies it on top of wolfBoot's I2C driver.

The transfer sequence is wolfBoot's rather than a rewrite: the register write is followed by a stop and a guard delay instead of a repeated start, because the part NAKs briefly after being addressed while it wakes and the specification asks for a guard time that a repeated start cannot provide.

```
make tpm WOLFBOOT_DIR=<wolfboot> WOLFSSL_DIR=<wolfssl> WOLFTPM_DIR=<wolftpm>
```

Measured on a C3M6, 1 October 2026:

```
TPM identity, read on the PSOC Control C3
  TPM2_Init_ex (no selftest): 0x00000000  bus and TIS OK
  wolfTPM2_Init            : 0x00000000  started
  wolfTPM2_GetCapabilities : 0x00000000
  Mfg IFX Vendor SLB9678 VU Fw 45.91
done
```

The two initialisation calls are reported separately on purpose. `TPM2_Init_ex` brings up the TIS layer without issuing a startup, so a part that answers its registers but refuses commands can be told apart from a bus that does not work. From the demo's point of view those two failures look identical, and they call for completely different responses.

### Footprint

```
   text    8520    data 1348    bss 12040
```

Under 10 KB of flash and 13.4 KB of RAM. Most of the RAM is the 5 KB command and response buffers, sized in `user_settings_tpm.h` to hold a parameter set 87 signature; an identity read alone would need far less. The code is small because this path needs no cryptography and the linker discards what it does not use, so expect it to grow once keys, sealing and sessions are exercised.

Taken with the benchmark above, a board-side act therefore costs roughly 10 KB of flash for the TPM path, 25 KB for verification, and about 24 KB of RAM between them. On a part with 512 KB of flash and 128 KB of SRAM, neither is a constraint.

### The strap, when there is no bootloader

A standalone image has to drive the interface-select strap itself, and it is the first thing `main()` does. The TPM latches its bus from that pin while its own reset is low, and its reset trails the CPU's by about a millisecond, so there is very little time. With wolfBoot in front of the application `hal_init()` has already done it; the standalone path only just gets there.
