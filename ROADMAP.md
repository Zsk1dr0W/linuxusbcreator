# Hoja de ruta

Esta hoja de ruta usa puertas de seguridad en lugar de fechas. Un hito solo se
considera completado cuando sus criterios de aceptación están comprobados.

## M0 — Repositorio y diseño

- [x] Definir el alcance, la arquitectura, el modelo de seguridad y el enfoque
  de empaquetado.
- [x] Establecer reglas de contribución y atribución compatibles con GPL.
- [x] Añadir un icono inicial y el plan de metadatos de escritorio.
- [x] Activar GitHub Actions.
- [ ] Configurar protección de la rama `main`.
- [x] Registrar las decisiones de arquitectura como ADR.

Criterio de salida: el repositorio compila su aplicación y sus pruebas, y los
límites entre sus componentes están documentados explícitamente. La protección
de rama sigue pendiente de configuración en GitHub.

## M1 — Descubrimiento de dispositivos en modo lectura

- [x] Crear el shell de GTK y el modo de diagnóstico por línea de comandos.
- [x] Enumerar dispositivos de bloque mediante UDisks2.
- [x] Mostrar identidad estable: nodo `/dev`, modelo, serie, bus y capacidad.
- [x] Excluir loop, zram, device-mapper, dispositivos ópticos y el dispositivo
  del sistema/root.
- [x] Reaccionar a inserciones, extracciones y cambios de propiedades reportados
  por UDisks2.
- [x] Añadir pruebas unitarias de la política de elegibilidad.
- [ ] Añadir pruebas de integración con un D-Bus privado y datos capturados de
  UDisks2.

Criterio de salida histórico: la entrega M1 no contenía ningún camino que
abriera un dispositivo de bloque para escribir. Se cumplió antes de iniciar M2;
las pruebas de integración siguen siendo una mejora pendiente para robustecer
la cobertura del monitor.

## M2 — Escritor de imágenes raw/híbridas (MVP)

- [x] Inspeccionar imágenes y calcular SHA-256 para imágenes regulares.
- [x] Obtener autorización mediante un helper Polkit de alcance limitado.
- [x] Desmontar particiones objetivo y volver a validar la identidad del
  dispositivo.
- [x] Escribir en streaming con memoria acotada, progreso, cancelación y
  `fsync`.
- [x] Añadir verificación completa opcional mediante lectura posterior.
- [x] Crear un registro estructurado y exportable de operaciones.
- [x] Añadir pruebas de fallos para escrituras cortas, desconexiones y pérdida
  de permisos.

Criterio de salida: las escrituras raw verificadas funcionan en la matriz de
hardware de pruebas sin permitir seleccionar el disco actual del sistema.
Cumplido inicialmente con el Kingston DataTraveler documentado en
`docs/MATRIZ_PRUEBAS_HARDWARE.md`; la matriz se ampliará con nuevos dispositivos.

## M3 — Primera versión distribuible

- [x] Completar metadatos AppStream, archivo `.desktop`, iconos, traducciones y
  página de manual.
- [x] Generar un tarball de fuentes reproducible.
- [x] Configurar CI para producir artefactos `.deb` y `.rpm`.
- [x] Probar instalación y eliminación en Debian, Ubuntu, Fedora y openSUSE;
  instalar y eliminar además los paquetes DEB y RPM generados por CI.
- [x] Publicar checksums de lanzamiento y el modelo de amenazas.
- [x] Configurar la clave OpenPGP de release y publicar checksums firmados.

Criterio de salida: los paquetes se instalan limpiamente y el MVP funciona sin
iniciar la interfaz gráfica como root.

## M3.1 — Flujo gráfico de escritura funcional

- [x] Seleccionar imágenes ISO/raw locales y mostrar su nombre y capacidad.
- [x] Permitir seleccionar únicamente USB escribibles con identidad estable;
  mantener visibles pero bloqueados el disco del sistema y los dispositivos no
  elegibles.
- [x] Desmontar mediante el helper existente y volver a validar serie, capacidad
  y política inmediatamente antes de escribir.
- [x] Exigir una confirmación destructiva escrita antes de solicitar Polkit.
- [x] Mostrar las fases de SHA-256, escritura, `fsync` y verificación con
  progreso en vivo.
- [x] Permitir cancelación y evitar cerrar la ventana dejando una escritura
  privilegiada huérfana.
- [x] Mantener la verificación completa activada de forma predeterminada y
  mostrar el resultado o diagnóstico final.
- [x] Añadir pruebas para candidatos montados, protocolo del helper y fallos al
  iniciar la operación gráfica.
- [x] Validar el flujo gráfico completo con el Kingston DataTraveler de la
  matriz de hardware.

Criterio de salida: una persona puede completar una escritura raw verificada
desde la interfaz sin ejecutar la aplicación como root y sin poder seleccionar
el disco del sistema.
Cumplido con Linux USB Creator 0.4.0 y una imagen Fedora Workstation Live 44;
el USB fue reconocido por el menú de arranque de una laptop real, inició Fedora
44 y llegó al menú de instalación. La evidencia se conserva en
`docs/MATRIZ_PRUEBAS_HARDWARE.md`.

## M4 — Medios de instalación de Windows

- [x] Definir el canal seguro y su separación de privilegios en ADR 0004.
- [x] Inspeccionar sistemas de archivos ISO9660/UDF sin privilegios y detectar
  estructuras de instalación, arquitecturas UEFI, payload WIM/ESD, límites
  FAT32, rutas inseguras y colisiones de nombres.
- [x] Validar el inspector y los payloads con dos ISO x64 reales, una WIM y una
  ESD, conservando solo hashes y metadatos no propietarios.
- [x] Repetir la matriz con dos ISO ARM64 reales, una WIM y una ESD.
- [x] Validar imágenes oficiales vigentes de Microsoft cuya procedencia pueda
  documentarse.
- [x] Crear el diseño GPT con una partición EFI para el perfil UEFI.
- [x] Añadir un diseño MBR/FAT32 activo y registros de arranque Windows NT6
  para el perfil BIOS x64, con atribución GPL compatible de `ms-sys`.
- [x] Formatear FAT32 mediante un helper controlado que revalida el USB.
- [ ] Añadir NTFS únicamente después de sus pruebas de arranque independientes.
- [x] Copiar y verificar los archivos como usuario sin privilegios.
- [x] Dividir archivos `install.wim` grandes con wimlib y verificar el conjunto
  `.swm`; validado con la ISO oficial Windows 11 25H2 x64.
- [x] Validar las estructuras de arranque de imágenes actuales de Windows.
- [x] Identificar en la interfaz imágenes Linux, Windows y raw/desconocidas;
  mostrar distribución y arquitectura cuando estén disponibles.
- [x] Ofrecer en Windows los perfiles `UEFI · GPT` y `BIOS · MBR`; ocultar
  BIOS para ARM64 o imágenes que no incluyan `boot/etfsboot.com`.
- [x] Mostrar porcentajes por bytes o desde wimlib en las etapas medibles y
  estados 0/100 reales en las operaciones atómicas.
- [x] Probar el medio UEFI/FAT32 x64 en hardware real: el firmware reconoció
  el USB, inició desde él y llegó a Windows Setup.
- [x] Probar el medio BIOS/MBR x64 en hardware real con CSM/Legacy: el firmware
  reconoció el USB, inició desde él y llegó al instalador de Windows.
- [x] Registrar que la variante UEFI ARM64 pasó inspección, validación de
  payload y comprobaciones estructurales, pero no una prueba de arranque por
  no disponer de hardware ARM64.

Los perfiles UEFI/GPT y BIOS/MBR x64 superaron la creación, verificación y el
arranque en hardware real. La variante UEFI ARM64 se distribuye como soporte
experimental y no se anuncia como validada físicamente. NTFS queda fuera de
esta versión.

Criterio de salida: la interfaz identifica la imagen, ofrece únicamente los
perfiles compatibles y crea medios UEFI/GPT y BIOS/MBR x64 que arrancan en
hardware real; muestra progreso porcentual por etapa y divide los WIM grandes
sin procesar contenido ISO/WIM como root ni permitir seleccionar el disco del
sistema. Cumplido para el alcance publicado y validado de Linux USB Creator
0.5.0. La validación física ARM64 queda como ampliación explícita de la matriz,
sin bloquear la entrega x64 ni afirmar una certificación inexistente.

## M4.1 — Visibilidad y compatibilidad del medio Windows UEFI

- [x] Reproducir y documentar que el perfil UEFI/GPT 0.5.0 crea una partición
  EFI System que Windows no muestra con letra de unidad y que algunos
  exploradores Linux omiten.
- [x] Cambiar el diseño GPT para que la partición FAT32 del medio extraíble sea
  visible como volumen de datos, previsiblemente mediante el GUID Microsoft
  Basic Data, conservando `EFI/BOOT` y el arranque UEFI removible.
- [x] Añadir pruebas unitarias del tipo GPT y sus atributos para evitar que el
  medio vuelva a quedar marcado como una ESP oculta.
- [x] Confirmar que, después del desmontaje seguro y de volver a conectar el
  USB, Windows le asigna una letra y el explorador de archivos permite ver su
  contenido.
- [x] Confirmar que, después del desmontaje seguro y de volver a conectar el
  USB, UDisks2 y los exploradores de archivos Linux permiten montarlo y ver su
  contenido.
- [x] Repetir la creación, división WIM, sincronización y verificación completa
  con una ISO oficial de Windows mayor que el límite FAT32.
- [x] Repetir en hardware real la prueba de arranque UEFI/GPT x64 hasta Windows
  Setup con el nuevo tipo de partición.
- [x] Comprobar mediante la suite automatizada y en hardware real que el perfil
  BIOS/MBR x64, su plan MBR activo y sus registros NT6 no presentan regresiones.
- [x] Actualizar el diagnóstico final y la documentación para eliminar la
  advertencia de que el medio UEFI puede quedar oculto.
- [x] Generar, instalar y probar los paquetes DEB y RPM de Linux USB Creator
  0.5.1, con checksums y firma OpenPGP.

Criterio de salida: un medio Windows UEFI/GPT creado por Linux USB Creator
0.5.1 arranca en hardware real, aparece como volumen explorable al volver a
conectarlo tanto en Windows como en Linux y conserva la verificación completa,
el desmontaje seguro y todas las protecciones contra la selección del disco
del sistema.

Criterio cumplido con Linux USB Creator 0.5.1 y el Kingston DataTraveler de la
matriz: la división WIM, los arranques UEFI/GPT y BIOS/MBR y la visibilidad del
volumen después de reconectarlo en los exploradores de Windows y Linux fueron
comprobados en hardware real.

## M5 — Modo ISO y persistencia Linux

- [ ] Añadir extracción ISO para imágenes que no puedan usar copia raw.
- [ ] Gestionar Syslinux/GRUB cuando sea necesario.
- [ ] Añadir persistencia opcional para distribuciones explícitamente soportadas.
- [ ] Crear una base de compatibilidad con fixtures y procedencia documentada.

## Candidatos posteriores

- Guardar un dispositivo USB como imagen.
- Pruebas de bloques defectuosos y de capacidad falsa.
- Descarga de imágenes y verificación de checksums.
- Accesibilidad y localización adicional.
