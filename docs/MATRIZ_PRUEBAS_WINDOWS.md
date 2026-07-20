# Matriz de pruebas de imágenes Windows

La matriz conserva únicamente metadatos técnicos y hashes. No incluye ni
redistribuye archivos de Microsoft.

## Inspección y validación de solo lectura

| Fecha | Imagen | SHA-256 | ISO | Payload | Resultado |
|---|---|---|---:|---:|---|
| 2026-07-20 | `28000.1_MULTI_X64_ES-ES.ISO` (WIM) | `5d9d9d145211d41d7b205e938ab252987ad20be6121add4dd1933b8877f9db14` | 4.796.831.744 bytes | `install.wim`, 4.072.797.956 bytes | UDF, BIOS y UEFI x64 detectados; `boot.wim` e `install.wim` verificados |
| 2026-07-20 | `28000.1_MULTI_X64_ES-ES.ISO` (ESD) | `62a51c41a4d9d756c75d0d101379a71674addd555d87e645384dc5ef4a923547` | 4.084.545.536 bytes | `install.esd`, 3.360.510.706 bytes | UDF, BIOS y UEFI x64 detectados; `boot.wim` e `install.esd` verificados |

Ambas imágenes contienen 1.061 archivos y son compatibles con un destino
FAT32 sin división: ningún archivo supera 4.294.967.295 bytes. El payload de
instalación contiene dos imágenes x86_64 en español (`es-ES`), Windows 11 Home
y Windows 11 Pro, build 28000.1.

La validación se ejecutó con 7-Zip 26.02 y wimlib 1.14.5. Cada ISO se montó como
UDF mediante un loop de solo lectura; wimlib verificó los metadatos y datos de
dos imágenes internas, equivalentes a 8.626 MiB lógicos, además de los dos
índices de `boot.wim`. Los WIM/ESD no traen una tabla de integridad opcional,
por lo que wimlib omitió únicamente esa comprobación adicional y completó la
verificación ordinaria correctamente.

Los comandos integrales terminaron con:

```json
{"validated":true,"format":"udf","install_payload":"wim","boot_wim":true}
{"validated":true,"format":"udf","install_payload":"esd","boot_wim":true}
```

Después de cada prueba, la aplicación desmontó el sistema de archivos y el
kernel liberó el loop mediante `autoclear`. Se confirmó que no quedaron loops
con backing ni montajes activos. Estas pruebas todavía no crean un USB Windows.
