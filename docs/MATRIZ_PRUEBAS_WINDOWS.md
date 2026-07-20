# Matriz de pruebas de imágenes Windows

La matriz conserva únicamente metadatos técnicos y hashes. No incluye ni
redistribuye archivos de Microsoft.

## Inspección y validación de solo lectura

| Fecha | Imagen | SHA-256 | ISO | Payload | Resultado |
|---|---|---|---:|---:|---|
| 2026-07-20 | `28000.1_MULTI_X64_ES-ES.ISO` (WIM) | `5d9d9d145211d41d7b205e938ab252987ad20be6121add4dd1933b8877f9db14` | 4.796.831.744 bytes | `install.wim`, 4.072.797.956 bytes | UDF, BIOS y UEFI x64 detectados; `boot.wim` e `install.wim` verificados |
| 2026-07-20 | `28000.1_MULTI_X64_ES-ES.ISO` (ESD) | `62a51c41a4d9d756c75d0d101379a71674addd555d87e645384dc5ef4a923547` | 4.084.545.536 bytes | `install.esd`, 3.360.510.706 bytes | UDF, BIOS y UEFI x64 detectados; `boot.wim` e `install.esd` verificados |
| 2026-07-20 | `28000.1_MULTI_ARM64_ES-ES.ISO` (WIM) | `084ecf9aba2f3c7a2ac52667cff1609d744595026558d1c203a32746c749fbc9` | 5.022.500.864 bytes | `install.wim`, 4.233.814.160 bytes | UDF y UEFI ARM64 detectados; `boot.wim` e `install.wim` verificados |
| 2026-07-20 | `28000.1_MULTI_ARM64_ES-ES.ISO` (ESD) | `d1a7f3e0cb72c5b400f5d04620026bd8da7317db38f581df1e1a112c3f3892af` | 4.292.345.856 bytes | `install.esd`, 3.503.660.390 bytes | UDF y UEFI ARM64 detectados; `boot.wim` e `install.esd` verificados |
| 2026-07-20 | `Win11_25H2_Spanish_x64_v2.iso` | `f5495dfcea4294c7924420ad860f409623575436f85a8d0a66a6bd9a1c3433d6` | 8.469.745.664 bytes | `install.wim`, 7.557.825.550 bytes | UDF, BIOS y UEFI x64 detectados; división WIM requerida; `boot.wim` e `install.wim` verificados |
| 2026-07-20 | `Win11_25H2_Spanish_Arm64_v2.iso` | `85574663784a7235e3a616242e27dd0c733e293321f726a246e4abd1a193e841` | 7.996.319.744 bytes | `install.wim`, 7.060.150.403 bytes | UDF y UEFI ARM64 detectados; división WIM requerida; `boot.wim` e `install.wim` verificados |

Las dos imágenes x64 contienen 1.061 archivos y son compatibles con un destino
FAT32 sin división: ningún archivo supera 4.294.967.295 bytes. El payload de
instalación contiene dos imágenes x86_64 en español (`es-ES`), Windows 11 Home
y Windows 11 Pro, build 28000.1.

Las dos imágenes ARM64 build 28000.1 contienen 1.048 archivos. El inspector no
anunció BIOS ni UEFI x64 para ellas y sí confirmó UEFI ARM64. También son
compatibles con FAT32 sin división; el `install.wim` más grande queda
61.153.135 bytes por debajo del máximo admitido para un archivo.

Las dos imágenes Windows 11 25H2 v2 fueron descargadas desde la distribución
oficial de Microsoft, según confirmó la persona responsable de la prueba. Los
archivos locales no conservaban una URL de origen como atributo extendido; por
eso la evidencia reproducible se vincula a los nombres, tamaños y SHA-256 de
la tabla. La variante x64 contiene 1.064 archivos y la ARM64, 1.050. Sus dos
`install.wim` superan el límite de 4.294.967.295 bytes para un archivo FAT32,
por lo que el inspector activó correctamente `requires_wim_split`.

La validación se ejecutó con 7-Zip 26.02 y wimlib 1.14.5. Cada ISO se montó como
UDF mediante un loop de solo lectura; wimlib verificó los metadatos y datos de
todos los índices de los payloads y de `boot.wim`. En la pareja x64, el payload
contiene dos imágenes internas equivalentes a 8.626 MiB lógicos. Los WIM/ESD
no traen una tabla de integridad opcional, por lo que wimlib omitió únicamente
esa comprobación adicional y completó la verificación ordinaria correctamente.

Los comandos integrales terminaron con:

```json
{"validated":true,"format":"udf","install_payload":"wim","boot_wim":true}
{"validated":true,"format":"udf","install_payload":"esd","boot_wim":true}
```

Los resultados se obtuvieron para las seis imágenes x64 y ARM64 de la tabla.
Después de cada prueba, la aplicación desmontó el sistema de archivos y el
kernel liberó el loop mediante `autoclear`. Se confirmó que no quedaron loops
con backing ni montajes activos. Estas pruebas todavía no crean un USB Windows.

## Ensayo del canal UEFI/FAT32

El motor M4 procesó `Win11_25H2_Spanish_x64_v2.iso` completo hacia un destino
temporal no privilegiado. Copió 1.063 archivos ordinarios, omitió el WIM
sobredimensionado y dividió `sources/install.wim` en:

| Fragmento | Tamaño |
|---|---:|
| `install.swm` | 3.430.030.698 bytes |
| `install2.swm` | 3.965.791.374 bytes |
| `install3.swm` | 97.499.042 bytes |

Todos los fragmentos quedaron bajo el límite FAT32. wimlib verificó el conjunto
completo de once índices usando los tres SWM como referencias; después el motor
sincronizó el destino y comparó byte a byte todos los archivos no divididos,
incluidos `efi/boot/bootx64.efi` y `sources/boot.wim`. El ensayo produjo 7,9 GiB
y terminó sin loops o montajes UDF residuales. El diseño fijo de partición se
probó además sobre un disco temporal de 1 GiB: GPT, inicio en sector 2048 y una
partición del tipo EFI System.

El flujo se repitió después sobre el Kingston DataTraveler de
30.995.907.072 bytes. El helper revalidó identidad y política, creó GPT y una
partición EFI FAT32 limpia. La copia, división, `fsync` y verificación
terminaron correctamente. El destino contenía 1.066 archivos regulares,
`efi/boot/bootx64.efi` (3.008.968 bytes), `sources/boot.wim` (629.671.774
bytes) y los tres SWM de la tabla; no quedó `install.wim`. En la prueba física
posterior, el firmware de una laptop x64 reconoció el USB, arrancó desde él y
llegó a Windows Setup.

## Ensayo del canal BIOS/MBR

La misma ISO oficial x64 se escribió dos veces con el perfil BIOS/MBR sobre el
Kingston. El ensayo final creó una tabla DOS, partición FAT32 tipo `0x0c`
iniciada en el sector 2048 y marcada `bootable`. Se instalaron registros de
arranque Windows 7/NT6 compatibles en el MBR, PBR principal y copia de respaldo;
las tres firmas `55 aa` se comprobaron por lectura posterior.

La aplicación informó porcentajes reales durante validación WIM, copia,
división WIM, verificación de SWM y comparación de archivos. Las operaciones
atómicas informaron 0/100. `fsck.fat -n` terminó limpio con 1.169 archivos y
513.493 de 1.890.851 clústeres usados. En la prueba física posterior, el equipo
x64 reconoció el USB en modo Legacy/CSM, arrancó desde él y llegó al instalador
de Windows.

## Alcance de la validación física

Los perfiles UEFI/GPT x64 y BIOS/MBR x64 están validados de extremo a extremo
en hardware real. Las imágenes ARM64 oficiales superaron la inspección, la
validación de payloads y las comprobaciones estructurales, pero no se probó su
arranque por no disponer de hardware ARM64. El perfil UEFI ARM64 de 0.5.0 debe
considerarse experimental hasta ampliar esta matriz.

## M4.1 — Visibilidad del volumen UEFI/GPT

La prueba posterior a 0.5.0 confirmó que el medio arrancaba y llegaba a Windows
Setup, pero su partición FAT32 no aparecía en los exploradores de Windows ni de
Linux. El plan 0.5.0 usaba el GUID EFI System
`c12a7328-f81f-11d2-ba4b-00a0c93ec93b`; Windows no asigna normalmente una letra
a ese tipo de partición.

El plan 0.5.1 usa Microsoft Basic Data
`ebd0a0a2-b9e5-4433-87c0-68b6b72699c7`, sin atributos de ocultación o de
supresión de letra, y conserva FAT32 y `EFI/BOOT`. Las pruebas automatizadas
rechazan una regresión al GUID EFI System.

La aplicación 0.5.1 instalada desde un RPM local repitió el flujo completo con
`Win11_25H2_Spanish_x64_v2.iso`: copió 1.066 archivos, dividió
`install.wim` en tres SWM, sincronizó, verificó los fragmentos y comparó los
archivos ordinarios. UDisks2 informó `HintAuto=true`, `HintIgnore=false` y
montó `/dev/sda1` como el usuario activo. GIO indicó `is-hidden: FALSE` y acceso
de lectura y escritura; `fsck.fat -n` terminó limpio con 1.169 entradas.

Quedan por registrar la visibilidad después de desconectar y reconectar
físicamente el USB en Linux y Windows y el nuevo arranque UEFI en hardware.
