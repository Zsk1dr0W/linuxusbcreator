# Initial threat model

## Protected assets

- Data on internal and external storage devices.
- Integrity of the running Linux installation.
- Authenticity of images, packages, and downloaded metadata.
- User trust in the displayed target identity and progress.

## Primary hazards

- Device node reuse between selection and write.
- Accidentally selecting a system, swap, RAID, LVM, encrypted, or backup disk.
- A malicious image exploiting a parser.
- Command or path injection into privileged operations.
- Partial writes caused by removal, power loss, full media, or I/O errors.
- UI spoofing or stale device information.

## Required controls before write support

- Stable identity checks before authorization and again before opening.
- Exclusive access and complete target unmounting.
- No shell execution in the privileged helper.
- Bounded parsers and fuzzing for untrusted image metadata.
- Explicit confirmation showing model, capacity, device, and destruction warning.
- Durable writes, clear cancellation semantics, and read-back verification.
- Structured audit log without secrets.

