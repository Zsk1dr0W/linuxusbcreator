# Compatibilidad de imágenes Linux

La base legible por herramientas se publica en
`data/linux-compatibility.json`. Un perfil solo habilita el modo ISO o la
persistencia cuando sus marcadores, cargador y parámetros de arranque tienen
una receta cerrada y pruebas propias. Una imagen Linux sin perfil conserva el
modo raw/híbrido verificado.

## Fedora Workstation Live x86_64

- Perfil: `fedora-live-uefi`.
- Versiones estructuralmente comprobadas: Fedora Workstation Live 43 (1.6) y
  44 (1.7).
- Modo universal: copia raw/híbrida, sin alterar la tabla ni el cargador de la
  ISO.
- Modo ISO: GPT, partición FAT32 de 1 GiB `LUC-BOOT` y partición ext4
  `LUC-LIVE`; conserva el shim y GRUB UEFI incluidos por Fedora.
- Persistencia: opcional, con los directorios ext4 `/overlayfs` y `/ovlwork` y
  los parámetros dracut `rd.live.overlay` y `rd.live.overlay.overlayfs`.
- Verificación: comparación byte a byte de todos los archivos extraídos y
  comprobación independiente del `grub.cfg` adaptado.
- Estado: estructura y pipeline validados; falta la prueba final de arranque y
  conservación de cambios en hardware real antes de cerrar M5.

La procedencia funcional se contrastó con el repositorio oficial
`livecd-tools/livecd-tools`, que documenta la transformación de imágenes Live
a USB y los overlays persistentes, y con la implementación oficial de dracut
`90dmsquash-live`, que define `rd.live.overlay` y el backend OverlayFS. Las
fixtures contienen únicamente nombres y metadatos de estructura, no contenido
propietario ni binarios de las imágenes.

## Debian, Ubuntu, Arch y otros Linux

Se identifican para mostrar distribución y arquitectura, pero desde 0.6.0 usan
exclusivamente la escritura raw/híbrida. No se ofrece persistencia genérica:
Debian Live, Ubuntu casper y otras familias emplean contratos de arranque y
almacenamiento diferentes, que requieren perfiles y matrices independientes.

## Puerta para ampliar un perfil

1. Fixture mínima con procedencia y sin payload redistribuido.
2. Detección positiva de todos los archivos de arranque requeridos.
3. Plan fijo que no acepte comandos, rutas o tablas desde la imagen.
4. Copia y análisis sin privilegios, sincronización y verificación completa.
5. Prueba UEFI/BIOS aplicable en hardware real y prueba de persistencia tras
   reiniciar dos veces.
