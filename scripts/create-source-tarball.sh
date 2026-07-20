#!/bin/sh
set -eu

version=${1:-0.5.0}
output_dir=${2:-dist}
archive="linuxusbcreator-${version}"

mkdir -p "$output_dir"
git archive --format=tar --prefix="${archive}/" HEAD > "${output_dir}/${archive}.tar"
xz --threads=1 --check=crc32 --force "${output_dir}/${archive}.tar"
sha256sum "${output_dir}/${archive}.tar.xz" > "${output_dir}/${archive}.tar.xz.sha256"
