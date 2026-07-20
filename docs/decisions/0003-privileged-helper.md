# ADR 0003: Helper efímero autorizado mediante pkexec

Estado: aceptado

## Decisión

La aplicación inicia un helper efímero mediante `pkexec`. La política Polkit
autoriza únicamente la ruta instalada del helper. El helper no ejecuta shell ni
acepta nombres de programas; sus argumentos son una imagen regular, un disco de
bloque completo y la identidad confirmada previamente.

El helper consulta UDisks2, exige un medio USB removible, excluye root y swap,
compara serial y capacidad, desmonta mediante D-Bus y repite todas las
validaciones antes de abrir el bloque con acceso exclusivo.

## Consecuencias

El proceso GTK nunca se ejecuta como root y el proceso privilegiado termina al
finalizar cada operación. Una futura API D-Bus propia puede sustituir este
proceso efímero si se necesitan operaciones concurrentes o recuperación tras
reinicios.
