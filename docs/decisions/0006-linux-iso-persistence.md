# ADR 0006: modo ISO y persistencia Linux por perfiles

## Estado

Aceptada para M5; validación física pendiente.

## Decisión

Mantener raw/híbrido como modo compatible para toda imagen Linux y habilitar
extracción o persistencia solo mediante perfiles positivos. El primer perfil
es Fedora Workstation Live x86_64 UEFI.

El helper revalida el USB y aplica una GPT fija con `LUC-BOOT` FAT32 de 1 GiB y
`LUC-LIVE` ext4. No monta ni analiza la ISO. La aplicación sin privilegios
monta la ISO como loop de solo lectura, conserva shim/GRUB incluidos, copia
`EFI`, `boot` y `LiveOS`, adapta únicamente el origen live en `grub.cfg` y
verifica todos los archivos. La persistencia añade directorios OverlayFS en la
partición ext4 y argumentos dracut cerrados.

Los directorios `LiveOS`, `overlayfs` y `ovlwork` se crean durante la
preparación privilegiada del sistema de archivos con el contexto SELinux
literal `system_u:object_r:root_t:s0`. El helper monta temporalmente únicamente
la segunda partición derivada del USB revalidado, con `nosuid`, `nodev` y
`noexec`; no acepta rutas ni contextos proporcionados por la aplicación.

El modo ISO es UEFI-only. BIOS continúa cubierto por la imagen híbrida original
hasta disponer de una receta Syslinux/GRUB BIOS independiente y probada.

## Razones

- Los mecanismos de persistencia difieren entre familias Linux.
- Reutilizar el shim/GRUB firmado de Fedora evita generar cargadores o claves.
- Separar FAT32 y ext4 elimina el límite de 4 GiB para LiveOS y proporciona las
  semánticas POSIX necesarias para OverlayFS.
- Mantener el procesamiento de la imagen fuera de root conserva el límite de
  privilegios establecido en ADR 0003 y 0004.

## Consecuencias

Fedora Live obtiene un medio explorable, verificable y persistente, pero el
modo extraído no promete BIOS. Otras distribuciones siguen funcionando por
copia raw y requieren un nuevo perfil antes de mostrar opciones ISO o de
persistencia.
