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
