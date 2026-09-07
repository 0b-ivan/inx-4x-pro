# USB Passkey security model

The USB passkey is an experimental, dedicated `x4pro_passkey` build. It is not
part of the normal CrossPlay release path and must not be presented as a
production security key yet.

## Implemented

- FIDO Alliance USB HID descriptor and CTAP-HID framing
- `CTAPHID_INIT` and `CTAPHID_PING`
- CTAP2 `authenticatorGetInfo`
- P-256 / secp256r1 key generation
- SHA-256 and ES256 signing primitives
- fixed-size credential records in NVS
- AES-256-GCM encryption and authentication per credential record
- random 96-bit GCM nonce per write
- record AAD binding the encrypted record to its vault slot
- persistent per-credential signature counter
- explicit zeroing of temporary private-key/plaintext buffers

The credential vault currently has 16 fixed slots. A record contains the
credential ID, RP ID hash, user handle, P-256 private/public key material and
signature counter. Private keys are only written as AES-GCM ciphertext.

## Current trust boundary

The AES-256 vault root key is generated randomly on first use and stored in the
same regular NVS partition as the encrypted records.

That protects records from accidental/plaintext disclosure and gives each
record authenticated encryption, but it does **not** provide a hardware trust
boundary. An attacker who can read raw flash can also recover the software vault
key and decrypt the records.

Therefore this branch is still development-only.

## Why eFuse/HMAC is not enabled automatically

ESP32-S3 can protect NVS encryption material using its HMAC/eFuse facilities.
Provisioning an eFuse key consumes an eFuse key block and is intentionally an
irreversible device-provisioning action. Firmware must never burn such a key as
an implicit first-boot side effect.

Hardware-backed storage therefore needs a separate, explicit provisioning flow
before this feature can be called release-ready.

## Release gates

Before a passkey release, all of the following still need to be true:

1. `authenticatorMakeCredential` is implemented with strict CBOR validation.
2. `authenticatorGetAssertion` is implemented with strict RP/credential checks.
3. Every credential creation and assertion requires explicit physical user
   presence on the device; no background request may silently sign.
4. The vault root key is hardware-bound (ESP32-S3 HMAC/eFuse-backed NVS or an
   equivalently reviewed design) through an explicit provisioning procedure.
5. Credential exclusion/allow lists, RP ID hashing, authenticator data and ES256
   signatures are covered by host tests and tested against a real WebAuthn
   client/browser.
6. Failure paths wipe sensitive temporary buffers and never return private key
   material over USB.
7. The dedicated passkey image is tested separately from the normal CrossPlay
   release image so the normal USB/OTA path remains unchanged.

## Deliberately not advertised yet

`authenticatorGetInfo` continues to report `rk=false` and `uv=false`.
`makeCredential` and `getAssertion` remain disabled until their CBOR parsing and
physical user-presence path are wired to the crypto/store layer.
