#!/usr/bin/env bash
set -u
source "$(dirname "$0")/env.sh"
echo "Project: $RP2350_ROOT"
echo "Groups: $(id -nG)"
if [[ -x "$RP2350_ROOT/.local/bin/sigrok-cli" ]]; then
    "$RP2350_ROOT/.local/bin/sigrok-cli" --list-supported | head -n 5
else echo 'Host stack has not been built.'; fi
shopt -s nullglob
ports=(/dev/serial/by-id/* /dev/ttyACM* /dev/ttyUSB*)
if [[ ${#ports[@]} == 0 ]]; then echo 'No CDC device found. Flash UF2 using BOOTSEL first.'; fi
for port in "${ports[@]}"; do
    ls -l "$port"
    if [[ -r "$port" && -w "$port" ]]; then
        echo "Readable/writable: $port"
    else
        echo "No serial permission. Run: sudo usermod -aG dialout $(id -un)"
        echo 'Then log out and log in again.'
    fi
done
