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
  run 1: 7473689 cycles
  run 2: 7470456 cycles
  run 3: 7470456 cycles
  run 4: 7470456 cycles
  run 5: 7470456 cycles
PASS
```

**7.47 million cycles**, repeatable to within 0.04 percent once the first run has warmed the flash cache. The cycle count is the figure to quote because it does not depend on how the core is clocked; at 48 MHz it is 155 ms, and at the 180 MHz this family supports it would be about 41 ms. Either way it sits comfortably inside a demo beat.

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

### Timebase

The benchmark prefers the trace unit's cycle counter and falls back to SysTick, reporting which it used. Not every Cortex-M33 implements the trace unit and TrustZone can put it out of reach, so a figure taken from a counter that was never running would otherwise look like a real measurement. On this part the trace unit works.

## Still to confirm

The core clock was not measured. 48 MHz is the power-on rate for this family and is what the millisecond figures assume; confirming it, or raising it, only scales the result.
