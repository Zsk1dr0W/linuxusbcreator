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

## Controles del canal Windows

- ISO/UDF y WIM/ESD se inspeccionan y procesan sin privilegios.
- Se rechazan rutas absolutas, `..`, enlaces y colisiones que FAT no distingue.
- El helper acepta solo los perfiles cerrados UEFI/GPT/FAT32 y BIOS/MBR/FAT32,
  herramientas de rutas
  confiables propiedad de root e identidad completa del USB.
- El plan UEFI fija Microsoft Basic Data y no admite atributos GPT ocultos o de
  supresión de letra de unidad; el cliente no puede suministrar otro GUID.
- El montaje de destino y la copia pertenecen al usuario activo; cada archivo
  se abre sin seguir enlaces y se sincroniza antes de verificarlo.
- Un `install.wim` grande se sustituye por SWM menores de 4 GiB y wimlib valida
  el conjunto completo con todos sus fragmentos como referencias.
