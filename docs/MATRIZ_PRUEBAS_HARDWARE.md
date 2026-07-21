# Matriz de pruebas de hardware

## M2 — Escritura raw verificada

| Fecha | Dispositivo | Bus | Capacidad | Imagen | Resultado |
|---|---|---:|---:|---:|---|
| 2026-07-20 | Kingston DataTraveler 3.0, serie `E0D55E6B6367172049400134` | USB | 30.995.907.072 bytes | 67.108.864 bytes | Escritura, `fsync` y verificación SHA-256 correctas |

SHA-256 de la imagen y de la lectura posterior:

```text
3b6a07d0d404fab4e23b6d34bc6696a6a312dd92821332385e5af7c01c421351
```

El helper confirmó bus USB, medio removible, serial, capacidad, ausencia de
swap y exclusión del disco raíz. UDisks2 desmontó la partición Ventoy antes de
la segunda validación. La prueba sobrescribió los primeros 64 MiB y dejó el
dispositivo sin particiones reconocibles.

## M3.1 — Flujo gráfico de escritura verificada

| Fecha | Versión | Dispositivo | Imagen | Resultado |
|---|---|---|---|---|
| 2026-07-20 | 0.4.0 (`579cb28`) | Kingston DataTraveler 3.0, `/dev/sda`, serie `E0D55E6B6367172049400134`, 30.995.907.072 bytes | Fedora Workstation Live 44-1.7 x86_64, 2.851.612.672 bytes | Escritura gráfica, `fsync` y verificación completa por lectura posterior correctas |

SHA-256 registrado para la imagen y la lectura posterior:

```text
1620295f6a00c27c3208f0c00b8ece4eab1ec69b9002152d97488bf26a426ddf
```

La aplicación gráfica se ejecutó sin privilegios y solicitó Polkit únicamente
para el helper de escritura. El registro estructurado contiene los eventos
`started` y `completed` para 2.851.612.672 bytes. Después de la operación, el
sistema reconoció `/dev/sda1` como `iso9660`, con etiqueta
`Fedora-WS-Live-44`, y `/dev/sda2` como `vfat`. El NVMe que contiene `/`,
`/home` y `/boot` permaneció excluido de los destinos elegibles.

## Validación de arranque físico

| Fecha | Medio creado con | Equipo | Resultado |
|---|---|---|---|
| 2026-07-20 | Linux USB Creator 0.4.0 | Laptop real | El firmware reconoció el USB en su menú de arranque; Fedora 44 inició correctamente y llegó al menú de instalación |

Esta prueba valida el recorrido completo desde la creación y verificación del
medio en la interfaz hasta el inicio del instalador en hardware real. No se
completó una instalación de Fedora sobre el disco de la laptop.

## M4 — Creación de medio Windows UEFI/GPT

| Fecha | Dispositivo | Imagen | Resultado |
|---|---|---|---|
| 2026-07-20 | Kingston DataTraveler 3.0, serie `E0D55E6B6367172049400134` | Windows 11 25H2 Spanish x64 v2 oficial | GPT/EFI/FAT32, copia, división WIM en tres SWM, sincronización y verificación completas; el firmware reconoció el USB, arrancó y llegó a Windows Setup |

`fsck.fat -n` informó un sistema FAT32 limpio. Se verificaron los registros de
GPT, la partición EFI, 1.066 archivos regulares y los payloads de arranque. La
prueba posterior en una laptop real acreditó también el arranque completo hasta
el instalador, sin realizar cambios en el disco interno.

## M4 — Creación de medio Windows BIOS/MBR

| Fecha | Dispositivo | Imagen | Resultado |
|---|---|---|---|
| 2026-07-20 | Kingston DataTraveler 3.0, serie `E0D55E6B6367172049400134` | Windows 11 25H2 Spanish x64 v2 oficial | MBR/FAT32 activo, registros MBR/PBR NT6, copia, división WIM, sincronización y verificación completas; reconocido y arrancado en Legacy/CSM hasta el instalador |

Se verificaron tabla DOS, tipo `0x0c`, bandera activa y firmas `55 aa` del MBR,
PBR principal y respaldo. `fsck.fat -n` terminó limpio. La prueba posterior en
hardware x64 con arranque Legacy/CSM reconoció el dispositivo, inició desde él
y llegó al instalador de Windows.

## M4 — Límite de la validación ARM64

Las imágenes oficiales ARM64 superaron la inspección UDF, la validación de
`boot.wim` e `install.wim`, la detección UEFI y las comprobaciones de estructura
documentadas en `MATRIZ_PRUEBAS_WINDOWS.md`. No se dispone de hardware ARM64
para comprobar el arranque físico; por tanto, 0.5.0 presenta este perfil como
experimental y no como validado en hardware.

## M4.1 — Medio Windows UEFI/GPT visible

| Fecha | Versión | Dispositivo | Imagen | Resultado |
|---|---|---|---|---|
| 2026-07-20 | 0.5.1, RPM local | Kingston DataTraveler 3.0, serie `E0D55E6B6367172049400134` | Windows 11 25H2 Spanish x64 v2 oficial, 8.469.745.664 bytes | Microsoft Basic Data/FAT32 sin flags, creación, división WIM, sincronización y verificación completas; visible después de reconectar en Windows y Linux; arranque UEFI/GPT hasta Windows Setup |

La tabla GPT usa el tipo
`EBD0A0A2-B9E5-4433-87C0-68B6B72699C7`, inicia en el sector 2048 y no contiene
atributos GPT. UDisks2 informó `HintAuto=true`, `HintIgnore=false` y montó el
volumen para el usuario activo en `/run/media/vdiiazg/LUC-WINDOWS`. Se
inspeccionaron 1.066 archivos, incluidos `efi/boot/bootx64.efi`, `boot.wim` y
tres fragmentos SWM. GIO informó `standard::is-hidden: FALSE` y permisos de
lectura y escritura. Después del desmontaje seguro, `fsck.fat -n` terminó
limpio con 1.169 entradas y 513.493 de 1.890.787 clústeres usados.

Después de desconectar y volver a conectar físicamente el Kingston, el volumen
y sus archivos aparecieron en los exploradores de Windows y Linux. El firmware
reconoció el perfil UEFI/GPT, arrancó desde él y llegó a Windows Setup. También
se repitió el perfil BIOS/MBR en hardware real, fue reconocido en Legacy/CSM y
llegó al instalador, sin regresiones respecto de 0.5.0.

El CI `29782579640` compiló y probó 0.5.1 en Debian, Ubuntu, Fedora y openSUSE;
construyó, instaló y eliminó los paquetes DEB/RPM, generó el tarball reproducible
y produjo el manifiesto SHA-256 con firma OpenPGP válida.

## M5 — Fedora Live UEFI con persistencia

| Fecha | Versión | Dispositivo | Imagen | Resultado |
|---|---|---|---|---|
| 2026-07-21 | 0.6.0, compilación de CI | Kingston DataTraveler 3.0, serie `E0D55E6B6367172049400134` | Fedora Workstation Live 44 x86_64 oficial | Medio ISO UEFI creado desde la aplicación con persistencia activada; el firmware arrancó el medio y Fedora mostró sus opciones, pero quedó en un bucle antes del escritorio |

La creación se realizó mediante el perfil extraído Fedora Live, manteniendo el
arranque UEFI incluido por la imagen y creando el área de datos persistente.
La persistencia no se considera validada: hay que aislar primero si el fallo
pertenece al modo ISO o a OverlayFS, capturar el diagnóstico de dracut y lograr
la llegada al escritorio antes de comprobar el cambio tras dos reinicios.
