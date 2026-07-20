#!/bin/sh
set -eu

output=$(LC_ALL=C "$1" --help)

printf '%s\n' "$output" | grep -F -- '--diagnose' >/dev/null
printf '%s\n' "$output" | grep -F -- '--sha256 IMAGE' >/dev/null
printf '%s\n' "$output" | grep -F -- '--inspect-image IMAGE' >/dev/null
printf '%s\n' "$output" | grep -F -- '--inspect-windows IMAGE' >/dev/null
printf '%s\n' "$output" | grep -F -- '--validate-windows IMAGE' >/dev/null
printf '%s\n' "$output" | grep -F -- '--write-windows IMAGE DEVICE SERIAL SIZE --firmware uefi|bios' >/dev/null
printf '%s\n' "$output" | grep -F -- '--write-image IMAGE DEVICE SERIAL SIZE' >/dev/null
printf '%s\n' "$output" | grep -F -- '--no-verify' >/dev/null
printf '%s\n' "$output" | grep -F -- '--version' >/dev/null
