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
- [ ] Validar el flujo gráfico completo con el Kingston DataTraveler de la
  matriz de hardware.

Criterio de salida: una persona puede completar una escritura raw verificada
desde la interfaz sin ejecutar la aplicación como root y sin poder seleccionar
el disco del sistema.

## M4 — Medios de instalación de Windows

- [ ] Inspeccionar sistemas de archivos ISO9660/UDF.
- [ ] Crear diseños GPT/MBR para objetivos BIOS y UEFI.
- [ ] Formatear FAT32/NTFS mediante helpers controlados.
- [ ] Copiar archivos conservando los metadatos relevantes.
- [ ] Dividir archivos `install.wim` grandes con wimlib.
- [ ] Validar estructuras de arranque y probar imágenes actuales del instalador
  de Windows.

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
