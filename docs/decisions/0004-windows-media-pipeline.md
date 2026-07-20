# ADR 0004: Canal de creación de medios de Windows

Estado: aceptado

## Contexto

Las ISO híbridas de Linux pueden escribirse como bloques, pero una ISO de
Windows normalmente debe extraerse sobre un sistema de archivos escribible. Un
archivo `sources/install.wim` puede superar el límite de 4 GiB de FAT32. Además,
los parsers de ISO/UDF y WIM procesan datos no confiables y no deben ejecutarse
con privilegios administrativos.

## Decisión

M4 se implementará como un canal por etapas:

1. La aplicación sin privilegios inspecciona ISO9660/UDF con 7-Zip, usando
   argumentos directos y nunca un shell. Rechaza rutas absolutas, componentes
   `..`, enlaces, colisiones que FAT no distingue y payloads ambiguos.
2. El primer perfil funcional será UEFI con una partición FAT32. Cuando el único
   archivo incompatible sea `sources/install.wim`, wimlib lo dividirá en
   fragmentos `.swm` menores de 4 GiB.
3. El helper recibe un plan cerrado, vuelve a validar identidad y política del
   USB, desmonta sus particiones y realiza solamente el particionado y formato
   previstos. No recibe comandos, opciones arbitrarias ni rutas de ejecutables.
4. La extracción de ISO y el procesamiento WIM se ejecutan como el usuario
   activo sobre una partición montada mediante UDisks2. El helper no procesa el
   contenido no confiable de la ISO.
5. El perfil BIOS se habilitará únicamente cuando exista una fuente auditable y
   redistribuible para el código de arranque requerido, con pruebas separadas.

## Consecuencias

El perfil UEFI/FAT32 cubre equipos modernos y conserva un límite de privilegios
pequeño. 7-Zip pasa a ser dependencia de ejecución y wimlib será necesaria para
ISO con WIM grande. NTFS y BIOS permanecen desactivados hasta contar con un
procedimiento de arranque comprobado; su presencia en el sistema no basta para
habilitarlos.
