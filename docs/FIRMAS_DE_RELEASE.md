# Firmas de release

Las releases oficiales se firman con una clave Ed25519 dedicada exclusivamente
a Linux USB Creator:

- Identidad: `Linux USB Creator Release <106137683+Zsk1dr0W@users.noreply.github.com>`
- Fingerprint: `3491 649D 47D5 5918 EA55 EE12 582F 7483 E526 D5F1`
- Caducidad: 19 de julio de 2028.
- Clave pública: [`linuxusbcreator-release-key.asc`](linuxusbcreator-release-key.asc)

CI publica un archivo `SHA256SUMS` que cubre el tarball de fuentes y los
paquetes DEB/RPM. En ejecuciones de `main` y manuales también publica
`SHA256SUMS.asc` como firma OpenPGP separada. Las ejecuciones de pull requests
no reciben secretos y producen solamente checksums sin firmar.

- `RELEASE_GPG_PRIVATE_KEY`: clave privada ASCII-armored exclusiva para releases.

La clave privada nunca debe añadirse al repositorio. La clave de automatización
no tiene contraseña y debe permanecer limitada al keyring local del responsable
de releases y al secreto cifrado `RELEASE_GPG_PRIVATE_KEY` de GitHub Actions.
El workflow falla si falta la clave o si su fingerprint no coincide con el
publicado, evitando generar silenciosamente una entrega oficial sin firma.

Verificación de checksums:

```sh
sha256sum --check SHA256SUMS
```

Verificación de firma cuando `SHA256SUMS.asc` esté disponible:

```sh
gpg --import docs/linuxusbcreator-release-key.asc
gpg --verify SHA256SUMS.asc SHA256SUMS
```

La salida de `gpg --verify` debe identificar exactamente el fingerprint
documentado arriba. Una firma válida con cualquier otra clave no constituye una
release oficial de Linux USB Creator.
