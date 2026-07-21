# ADR 0005: Volumen Windows UEFI visible

Estado: aceptado y validado en hardware

## Contexto

Linux USB Creator 0.5.0 creó el único volumen FAT32 de los medios Windows
UEFI/GPT con el GUID EFI System
`c12a7328-f81f-11d2-ba4b-00a0c93ec93b`. El medio arrancó correctamente, pero
Windows no le asignó una letra y los exploradores de Windows y Linux no
mostraron su contenido después de volver a conectarlo.

Microsoft documenta que Windows expone y puede asignar letras únicamente a las
particiones GPT Microsoft Basic Data. La especificación UEFI define el arranque
removible por el sistema de archivos FAT y la ruta predeterminada
`EFI/BOOT/BOOT{arquitectura}.EFI`; además permite descubrir sistemas de archivos
reconocibles en cualquier partición de un dispositivo de tipo disco.

Referencias primarias:

- Microsoft, Windows and GPT FAQ:
  https://learn.microsoft.com/windows-hardware/manufacture/desktop/windows-and-gpt-faq
- Microsoft, `PARTITION_INFORMATION_GPT`:
  https://learn.microsoft.com/windows/win32/api/winioctl/ns-winioctl-partition_information_gpt
- UEFI Specification 2.10, Media Access:
  https://uefi.org/specs/UEFI/2.10/13_Protocols_Media_Access.html

## Decisión

El perfil UEFI/GPT usará una única partición FAT32 con GUID Microsoft Basic Data
`ebd0a0a2-b9e5-4433-87c0-68b6b72699c7`. El plan cerrado no establecerá
atributos GPT de ocultación, plataforma requerida o supresión de letra. La ruta
de arranque removible de la ISO se conservará bajo `EFI/BOOT`.

El perfil BIOS/MBR no cambia. El cliente no puede enviar un GUID, atributos ni
un plan alternativo al helper privilegiado.

## Consecuencias

Windows puede tratar el FAT32 como volumen de datos y asignarle una letra; Linux
y UDisks2 pueden exponerlo normalmente. La prueba final confirmó la visibilidad
después de reconectar el medio tanto en Windows como en Linux y el arranque
UEFI/GPT x64 hasta Windows Setup. El perfil BIOS/MBR también volvió a superar
su prueba física en Legacy/CSM.
