# QRAuthGen
# Embedded TOTP Authenticator Enrollment and Verification

A compact C implementation of **TOTP enrollment, QR-code generation, and six-digit TOTP verification**, designed for eventual deployment on a **32-bit ARM7 embedded system**.

The project is developed and validated first as a Microsoft Visual Studio console application. The portable portions are intended to be transferred to an embedded target without requiring third-party cryptographic or QR-code libraries.

The implementation has been tested end-to-end with **Microsoft Authenticator**.

---

## Project Goals

The primary goals of this project are:

- Implement TOTP enrollment in straight C.
- Generate a cryptographically random TOTP secret.
- Encode the binary secret using Base32.
- Construct a standard `otpauth://` provisioning URI.
- Generate a QR code without an external QR library.
- Produce a 128 × 128 monochrome QR bitmap.
- Write the QR image as a 1-bit Windows BMP file.
- Enroll the generated account using Microsoft Authenticator.
- Implement SHA-1 in C.
- Implement HMAC-SHA1 in C.
- Generate RFC 6238-style six-digit TOTP codes.
- Verify codes produced by Microsoft Authenticator.
- Support a small clock-skew acceptance window.
- Provide deterministic debug artifacts for comparison during the ARM7 port.
- Keep platform-specific functions isolated from the portable authentication code.
- Ultimately operate on an embedded target with no console or debug output.

The production target is a **32-bit ARM7 device** with SPI flash using a FAT filesystem.

---

# System Overview

The enrollment process is:

```text
Secure Random Generator
        |
        v
160-bit Binary Secret
        |
        v
Base32 Encoding
        |
        v
32-character Base32 Secret
        |
        v
otpauth:// Enrollment URI
        |
        v
QR Version 7-M Encoder
        |
        v
128 x 128 1-bit Bitmap
        |
        v
BMP File
        |
        v
Microsoft Authenticator
```

Authentication follows the reverse relationship:

```text
Stored 160-bit Secret
        |
        +----------------------+
        |                      |
        v                      v
Current Unix Time       User Enters Code
        |                      |
        v                      |
30-Second Counter              |
        |                      |
        v                      |
HMAC-SHA1                      |
        |                      |
        v                      |
Dynamic Truncation             |
        |                      |
        v                      |
6-Digit TOTP ------------------+
        |
        v
Pass / Fail
```

---

# Enrollment Flow

Enrollment is intentionally separated into discrete operations.

## 1. Generate the Secret

```c
TOTP_CreateRandom(secret);
```

The secret is:

```text
20 bytes
160 bits
```

The Visual Studio test implementation uses the Windows cryptographically secure random-number generator.

The ARM7 implementation must replace the Windows-specific random source with an appropriate hardware or platform secure random-number source.

`rand()` must **not** be used to generate authentication secrets.

---

## 2. Base32 Encode the Secret

```c
TOTP_Base32Encode(
    secret,
    TOTP_SECRET_BYTES,
    secret_base32,
    sizeof(secret_base32));
```

A 160-bit secret converts exactly into:

```text
32 Base32 characters
```

No Base32 `=` padding is required.

The alphabet used is:

```text
ABCDEFGHIJKLMNOPQRSTUVWXYZ234567
```

The binary secret remains the actual authentication credential. Base32 is only the textual representation required for enrollment.

---

## 3. Create the Enrollment URI

```c
TOTP_CreateEnrollment(
    issuer,
    username,
    secret_base32,
    uri,
    sizeof(uri));
```

The generated URI follows the standard TOTP provisioning format:

```text
otpauth://totp/ISSUER:USERNAME?secret=BASE32SECRET&issuer=ISSUER
```

Example:

```text
otpauth://totp/MyApp:user01?secret=JBSWY3DPEHPK3PXPJBSWY3DPEHPK3PXP&issuer=MyApp
```

The issuer and username are URI percent-encoded where required.

The enrollment function does **not** generate random data and does **not** perform Base32 encoding.

Its only responsibility is constructing the provisioning URI.

This separation makes the embedded port easier to test and maintain.

---

# TOTP Parameters

The implementation uses the conventional TOTP parameters:

| Parameter | Value |
|---|---|
| Secret | 160 bits |
| Hash algorithm | HMAC-SHA1 |
| OTP digits | 6 |
| Time period | 30 seconds |
| Initial epoch | Unix epoch |
| Counter | `UnixTime / 30` |

The provisioning URI currently omits:

```text
algorithm=SHA1
digits=6
period=30
```

because these are the conventional defaults and omitting them produces a shorter QR payload.

---

# QR-Code Implementation

The QR encoder is implemented directly in C and does not require an external QR library.

The current implementation uses a fixed configuration:

| Property | Configuration |
|---|---|
| QR Version | 7 |
| Matrix size | 45 × 45 modules |
| Error correction | M |
| Encoding | Byte mode |
| Data codewords | 124 |
| ECC codewords/block | 18 |
| Number of blocks | 4 |
| Total codewords | 196 |
| Masks evaluated | 8 |
| Final image | 128 × 128 |
| Pixel format | 1 bit/pixel |
| QR scale | 2 pixels/module |

A Version 7 symbol contains:

```text
45 x 45 modules
```

At two pixels per module:

```text
45 x 2 = 90 pixels
```

The 90 × 90 QR symbol is centered inside the 128 × 128 output image, leaving the surrounding white quiet area.

---

# Reed-Solomon Error Correction

QR error correction is implemented using Reed-Solomon arithmetic over GF(256).

The implementation uses:

```text
GF(256)
Primitive polynomial: 0x11D
```

For Version 7-M, the current implementation uses four Reed-Solomon blocks.

Each block contains:

```text
31 data codewords
18 error-correction codewords
```

The final QR stream contains:

```text
196 codewords
```

Data and error-correction codewords are interleaved according to the QR block structure.

---

# QR Mask Selection

All eight standard QR masks are evaluated.

Each candidate is scored using QR penalty rules including:

- Consecutive identical modules.
- 2 × 2 blocks.
- Finder-like patterns.
- Dark/light module balance.

The lowest-penalty candidate becomes the final QR matrix.

The mask-selection implementation explicitly initializes the first valid candidate before comparing the remaining masks. This avoids relying on potentially uninitialized `best` matrix storage.

---

# QR Bitmap

The public encoder interface is:

```c
int QR_Encode128(
    const unsigned char *text,
    unsigned char *bitmap);
```

The generated bitmap is:

```text
128 pixels wide
128 pixels high
1 bit per pixel
```

Memory required:

```text
128 x 128 / 8 = 2048 bytes
```

The internal bitmap convention is:

```text
0 = white
1 = black
```

Each row contains:

```text
128 / 8 = 16 bytes
```

Bits are stored MSB first within each byte.

---

# BMP Output

For PC testing, the generated QR bitmap can be written as:

```text
authenticator.bmp
```

The BMP is:

```text
128 x 128
1 bit/pixel
```

The file contains:

```text
14-byte BMP file header
40-byte BITMAPINFOHEADER
8-byte two-entry palette
2048 bytes pixel data
```

Approximate total file size:

```text
2110 bytes
```

The palette maps:

```text
0 -> white
1 -> black
```

The BMP uses bottom-up Windows bitmap row ordering.

---

# ARM7 Storage

The production ARM7 system is expected to have external SPI flash using a FAT filesystem.

The enrollment QR can therefore be stored as a small file such as:

```text
authenticator.bmp
```

The BMP is an enrollment artifact and should not be considered permanent credential storage.

The permanent authentication credential is the original:

```text
20-byte binary TOTP secret
```

The secret should be stored in appropriately protected nonvolatile storage.

The Base32 representation, provisioning URI, and BMP should not be treated as long-term secret storage.

Where practical, temporary secret-bearing enrollment buffers should be cleared after enrollment is complete.

---

# SHA-1

SHA-1 is implemented directly in C.

The implementation provides:

```c
SHA1_Init()
SHA1_Update()
SHA1_Final()
```

No external cryptographic library is required by the portable implementation.

SHA-1 is used here specifically as part of **HMAC-SHA1 for standards-compatible TOTP**, not as a general-purpose password hashing or digital-signature mechanism.

---

# HMAC-SHA1

TOTP uses HMAC-SHA1:

```text
HMAC-SHA1(
    secret,
    counter
)
```

The TOTP counter is represented as an eight-byte big-endian integer.

The calculation is:

```text
counter = UnixTime / 30
```

followed by:

```text
counter
    |
    v
8-byte big-endian value
    |
    v
HMAC-SHA1
    |
    v
20-byte digest
```

---

# TOTP Generation

TOTP generation follows the standard HOTP dynamic-truncation process.

The offset is:

```c
offset = digest[19] & 0x0F;
```

Four bytes are extracted from the HMAC digest and converted to a positive 31-bit integer.

The six-digit code is:

```c
otp = binary % 1000000;
```

Codes are displayed using:

```c
"%06u"
```

because leading zeros are significant.

For example:

```text
003721
```

is a valid six-digit TOTP.

---

# Time Requirements

TOTP requires a reasonably accurate UTC time source.

The moving counter is:

```text
UnixTime / 30
```

where Unix time is the number of seconds since:

```text
1970-01-01 00:00:00 UTC
```

The Visual Studio test program uses:

```c
time(NULL)
```

The ARM7 implementation may obtain time from an appropriate source such as:

- RTC
- Network synchronization
- NTP
- Trusted server synchronization

The embedded application does not specifically require a hardware RTC as long as a sufficiently accurate and trustworthy Unix-time value is available.

---

# Authentication Window

The current verification implementation supports a configurable time window.

For:

```c
window = 1;
```

the verifier tests:

```text
previous 30-second counter
current 30-second counter
next 30-second counter
```

This tolerates small clock differences and the common case where the user enters a code just as Microsoft Authenticator changes to the next interval.

The production implementation should keep this window deliberately small.

---

# Replay Protection

A valid TOTP remains mathematically valid during its accepted time interval.

If the production system permits adjacent time intervals, a previously accepted code could otherwise be accepted again.

A production implementation should therefore maintain the last successfully accepted counter, for example:

```c
uint64_t last_accepted_counter;
```

A matching OTP should only be accepted when its associated counter satisfies the application's replay policy.

After successful authentication, the accepted counter should be recorded in protected persistent state where appropriate.

---

# Visual Studio Test Application

The Windows application exists primarily to validate the portable implementation before it is moved to ARM7.

A typical test sequence is:

```text
1. Generate random 160-bit secret.

2. Base32 encode the secret.

3. Create the otpauth:// URI.

4. Generate the QR bitmap.

5. Write authenticator.bmp.

6. Open the BMP.

7. Scan it using Microsoft Authenticator.

8. Press Enter in the test application.

9. Read the current six-digit code from Authenticator.

10. Enter the code into the Visual Studio application.

11. Generate the expected TOTP independently.

12. Compare the codes.

13. Report PASS or FAIL.
```

A successful test demonstrates interoperability between the C implementation and Microsoft Authenticator.

---

# Debug Files

The Visual Studio build can generate several troubleshooting artifacts.

## CurrentDebugRun.txt

```text
CurrentDebugRun.txt
```

contains a copy of debug information written to the console.

The file is opened using write/truncate mode at application startup, so every execution begins with an empty log.

Only information from the current execution is retained.

During development this may include:

- Generated Base32 secret.
- Enrollment URI.
- Unix time.
- TOTP counter.
- Entered OTP.
- Calculated OTP.
- Authentication result.
- QR generation status.

Because development logs may contain authentication secrets, they must be considered sensitive.

This logging functionality is intended for PC development only.

---

## qr_bitmap.csv

```text
qr_bitmap.csv
```

contains the raw 2048-byte output of `QR_Encode128()`.

The file contains hexadecimal byte values:

```text
0x00,0x00,0x3F,0x80,...
```

There are:

```text
16 bytes per row
128 rows
2048 bytes total
```

Because one bitmap row is exactly 16 bytes, each CSV line corresponds to one complete 128-pixel row.

The data can also be pasted directly into firmware:

```c
static const unsigned char qr_reference_bitmap[2048] =
{
    /* known-good Visual Studio bitmap */
};
```

This provides a useful deterministic reference when porting the QR encoder to ARM7.

---

# ARM7 Port Verification Strategy

Before testing the complete embedded system, individual stages can be compared against the Visual Studio reference implementation.

Recommended checkpoints are:

```text
Checkpoint 1
------------
20-byte binary secret


Checkpoint 2
------------
32-character Base32 representation


Checkpoint 3
------------
otpauth:// URI


Checkpoint 4
------------
2048-byte QR bitmap


Checkpoint 5
------------
authenticator.bmp


Checkpoint 6
------------
HMAC-SHA1 digest


Checkpoint 7
------------
6-digit TOTP


Checkpoint 8
------------
Microsoft Authenticator verification
```

Using a fixed known secret during initial port validation makes these comparisons deterministic.

Random secret generation should be enabled only after the deterministic implementation has been verified.

---

# Known-Good QR Reference

For ARM7 troubleshooting, a known URI can be passed through the Visual Studio implementation and its complete 2048-byte QR bitmap saved.

The same URI can then be processed by the ARM7 implementation.

The results should be compared byte-for-byte:

```c
memcmp(
    arm_bitmap,
    reference_bitmap,
    2048);
```

A matching result verifies the QR implementation independently from:

- Hardware random-number generation.
- SPI flash.
- FAT filesystem.
- BMP file writing.
- TOTP calculation.
- User authentication.

This greatly simplifies embedded debugging.

---

# Development vs. Production

The PC test build intentionally contains functionality that should not exist in the production product.

## Visual Studio Development Build

The development build may contain:

```text
Windows BCrypt random generator
Console output
CurrentDebugRun.txt
qr_bitmap.csv
authenticator.bmp
Base32 secret display
Enrollment URI display
Calculated TOTP display
Detailed internal error codes
Known test vectors
```

These features exist to validate the implementation.

## ARM7 Production Build

The production implementation is intended to contain:

```text
Platform secure random source
TOTP enrollment
Base32 encoding
QR generation
SPI flash / FAT BMP storage
Protected binary secret storage
SHA-1
HMAC-SHA1
TOTP verification
Replay protection
Pass / Fail authentication result
```

The production product is expected to have **no authentication debug console output**.

Secret values, provisioning URIs, calculated TOTP values, and cryptographic intermediate results must not be exposed through production diagnostics.

---

# Compile-Time Debug Control

Development instrumentation can be conditionally compiled.

For example:

```c
#ifdef AUTH_DEBUG

#define DEBUG_PRINT(...) \
    DEBUG_Print(__VA_ARGS__)

#else

#define DEBUG_PRINT(...) \
    ((void)0)

#endif
```

The Visual Studio build can define:

```c
#define AUTH_DEBUG
```

The ARM7 production build leaves `AUTH_DEBUG` undefined.

This removes development debug calls from the production build.

---

# Recommended Production Result Interface

The application-facing authentication interface should remain small.

For example:

```c
#define AUTH_PASS             0
#define AUTH_FAIL             1
#define AUTH_NOT_ENROLLED     2
#define AUTH_TIME_INVALID     3
#define AUTH_INTERNAL_ERROR   4
```

The internal implementation may maintain more detailed error information during development.

Externally, authentication failures should generally not reveal unnecessary information about why a supplied authentication code failed.

---

# Suggested Production API

A future application-level API could be reduced to functions such as:

```c
int AUTH_CreateEnrollment(
    const char *issuer,
    const char *username);

int AUTH_VerifyCode(
    uint32_t six_digit_code,
    uint64_t unix_time);

int AUTH_DeleteEnrollmentQR(void);

int AUTH_IsEnrolled(void);
```

The application therefore does not need to interact directly with SHA-1, HMAC, Base32, QR masks, Reed-Solomon calculations, or provisioning URI construction.

---

# Proposed Source Organization

As the ARM7 port progresses, the project can be separated into modules such as:

```text
src/
    auth.c
    auth.h

    totp.c
    totp.h

    sha1.c
    sha1.h

    qr.c
    qr.h

    qr_bmp.c
    qr_bmp.h

    storage.c
    storage.h

test/
    main.c

README.md
```

Possible responsibilities:

### `auth.c`

Application-facing enrollment and authentication control.

### `totp.c`

- Secret handling.
- Base32.
- Enrollment URI generation.
- HMAC-SHA1 integration.
- TOTP generation.
- TOTP verification.

### `sha1.c`

Portable SHA-1 implementation.

### `qr.c`

Fixed Version 7-M QR encoder.

### `qr_bmp.c`

128 × 128 monochrome BMP generation.

### `storage.c`

Target-specific storage interface for FAT/SPI flash and protected persistent state.

### `test/main.c`

Visual Studio validation and troubleshooting application.

---

# MCU Optimization Opportunities

The initial implementation prioritizes correctness and readability.

After the Visual Studio reference implementation is frozen and known-good, several optimizations are possible for the embedded target.

## QR Matrix Packing

The development implementation uses approximately:

```text
45 x 45 = 2025 bytes
```

for a byte-per-module QR matrix.

The module state can instead be represented using bitmaps.

For example:

```text
QR pixel bitmap       ~254 bytes
QR reserved bitmap    ~254 bytes
                     -----------
                      ~508 bytes
```

This saves approximately 1.5 KB of RAM.

---

## Stream BMP Rows Directly to FAT

The current 128 × 128 bitmap occupies:

```text
2048 bytes
```

The embedded implementation does not necessarily need to retain the complete rendered bitmap.

One BMP row is only:

```text
16 bytes
```

The ARM7 implementation can therefore generate one row at a time:

```text
QR matrix
    |
    v
16-byte BMP row
    |
    v
FAT write
    |
    v
SPI flash
```

This can reduce the BMP rendering workspace from:

```text
2048 bytes
```

to approximately:

```text
16 bytes
```

---

## SHA-1 Message Schedule

A straightforward SHA-1 implementation commonly uses:

```c
uint32_t w[80];
```

which requires:

```text
320 bytes
```

The SHA-1 message schedule can instead use a 16-word circular buffer:

```c
uint32_t w[16];
```

requiring only:

```text
64 bytes
```

This is an appropriate optimization for the embedded implementation after the reference implementation has been fully validated.

---

## QR Mask Selection Stack Usage

A development implementation that keeps:

```text
current matrix
original matrix
best matrix
```

simultaneously can consume several kilobytes.

The ARM7 implementation should avoid unnecessary full-matrix copies during mask evaluation.

This is an important optimization target for systems with limited stack space.

---

# Security Notes

This project handles authentication credentials.

Important implementation requirements include:

1. Generate secrets using a cryptographically secure random source.

2. Never use `rand()` for TOTP secret generation.

3. Protect the permanent 20-byte binary secret.

4. Do not expose secrets in production logs.

5. Do not expose provisioning URIs after enrollment unless specifically required.

6. Remove or protect temporary enrollment files when they are no longer required.

7. Clear temporary secret-bearing RAM where practical.

8. Keep the TOTP clock synchronized.

9. Use a deliberately limited clock-skew acceptance window.

10. Implement replay protection appropriate to the product.

11. Protect authentication interfaces against repeated automated guessing.

12. Treat the QR code as credential-bearing enrollment material. Anyone who obtains the QR during enrollment can potentially enroll another authenticator using the same TOTP secret.

---

# Important Scope Note

TOTP provides proof that the user possesses the shared TOTP secret.

It does not by itself provide:

- User identity proofing.
- Secure secret provisioning outside the QR enrollment process.
- Secure firmware.
- Secure boot.
- Protected key storage.
- Rate limiting.
- Physical tamper resistance.
- Network encryption.
- Session authorization.
- Device identity.
- Recovery procedures.

Those controls must be designed separately as required by the final product.

---

# Standards and Compatibility

The implementation is designed around the established HOTP/TOTP and QR provisioning conventions used by authenticator applications.

Relevant standards include:

- RFC 2104 — HMAC
- RFC 4226 — HOTP
- RFC 4648 — Base32
- RFC 6238 — TOTP
- QR Code Model 2 conventions

The current configuration uses:

```text
HMAC-SHA1
6 digits
30-second period
160-bit secret
```

The generated provisioning QR has been tested successfully with Microsoft Authenticator during development.

---

# Current Development Status

The Visual Studio implementation has been tested through the complete enrollment and authentication flow:

```text
Secure random secret generation
        PASS
          |
          v
Base32 encoding
        PASS
          |
          v
otpauth URI generation
        PASS
          |
          v
QR Version 7-M generation
        PASS
          |
          v
128 x 128 BMP generation
        PASS
          |
          v
Microsoft Authenticator enrollment
        PASS
          |
          v
SHA-1 / HMAC-SHA1 TOTP calculation
        PASS
          |
          v
Microsoft Authenticator code verification
        PASS
```

The next major phase is transferring the validated reference implementation to the 32-bit ARM7 target and optimizing RAM, stack, storage, and platform-specific interfaces without changing the verified external behavior.

---

# License

This project is released under the **MIT License**.

The intent is to allow this software to be freely used, modified, incorporated into other projects, and distributed for both private and commercial purposes, subject to the terms of the MIT License.

See the `LICENSE` file in this repository for the complete license text.

## Development Note

Significant portions of this project were developed with assistance from OpenAI ChatGPT, followed by significant line by line audit, compilation, testing, debugging, modification, and validation as part of the development process.

The project is provided as an implementation and reference for embedded TOTP enrollment, QR-code generation, and authentication. Users integrating this code into production systems are responsible for validating its security, correctness, suitability, and compliance with the requirements of their application.

---

# Disclaimer

This project is an embedded authentication implementation and should be reviewed and tested appropriately before use in a production security-sensitive system.

The Visual Studio implementation is primarily a reference and validation environment. Debug output, test files, displayed secrets, and other development instrumentation are not intended for production deployment.
