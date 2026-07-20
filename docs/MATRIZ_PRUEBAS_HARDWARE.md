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
