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
por lo que el inspector activó correctamente `requires_wim_split`. Esta prueba
valida la detección de la necesidad de división, no la división ni la creación
del medio.

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
