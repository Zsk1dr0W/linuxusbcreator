# Firmas de release

CI publica un archivo `SHA256SUMS` que cubre el tarball de fuentes y los
paquetes DEB/RPM. Si GitHub contiene los siguientes secretos, también publica
`SHA256SUMS.asc` como firma OpenPGP separada:

- `RELEASE_GPG_PRIVATE_KEY`: clave privada ASCII-armored exclusiva para releases.
- `RELEASE_GPG_PASSPHRASE`: contraseña de esa clave, si corresponde.

La clave privada nunca debe añadirse al repositorio. La clave pública y su
fingerprint deberán publicarse en la documentación de una release antes de
considerar habilitada la firma oficial.

Verificación de checksums:

```sh
sha256sum --check SHA256SUMS
```

Verificación de firma cuando `SHA256SUMS.asc` esté disponible:

```sh
gpg --verify SHA256SUMS.asc SHA256SUMS
```
