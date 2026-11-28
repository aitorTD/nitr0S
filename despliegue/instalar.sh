#!/bin/sh
# Instala nitr0sd en la Pi. Se ejecuta EN LA PI, con sudo.
#
#   sudo ./instalar.sh nitr0sd-arm64
#
# Es idempotente: se puede volver a ejecutar para actualizar el binario.
set -eu

BIN="${1:-nitr0sd-arm64}"
[ -f "$BIN" ] || { echo "no encuentro $BIN" >&2; exit 1; }

# Que el binario sea de la arquitectura de esta Pi, antes de instalarlo.
ARCO_PI="$(uname -m)"
ARCO_BIN="$(file -b "$BIN" | sed -n 's/.*ELF [0-9]*-bit LSB [^,]*, \([^,]*\).*/\1/p')"
echo "Pi: $ARCO_PI   binario: ${ARCO_BIN:-desconocido}"
case "$ARCO_PI:$ARCO_BIN" in
    aarch64:*ARM\ aarch64*) ;;
    armv*l:*ARM,*)          ;;
    *) echo "AVISO: la arquitectura del binario no parece la de esta Pi." >&2
       echo "       aarch64 necesita nitr0sd-arm64; armv7l necesita nitr0sd-armv7." >&2
       printf "¿seguir igualmente? [s/N] "; read -r r; [ "$r" = "s" ] || exit 1 ;;
esac

id -u nitr0s >/dev/null 2>&1 || useradd --system --home /var/lib/nitr0s --shell /usr/sbin/nologin nitr0s
install -d -o nitr0s -g nitr0s -m 0750 /var/lib/nitr0s
install -m 0755 "$BIN" /usr/local/bin/nitr0sd
install -m 0644 nitr0sd.service /etc/systemd/system/nitr0sd.service

systemctl daemon-reload
systemctl enable nitr0sd
systemctl restart nitr0sd
sleep 1
systemctl --no-pager --lines=10 status nitr0sd || true

echo
echo "Comprobación:"
curl -sS http://127.0.0.1:8080/hello && echo
echo
echo "Siguiente: crear el usuario de la consola y dar de alta el aparato."
echo "  sudo -u nitr0s /usr/local/bin/nitr0sd -db /var/lib/nitr0s/nitr0s.db -add-user aitor"
echo "  sudo -u nitr0s /usr/local/bin/nitr0sd -db /var/lib/nitr0s/nitr0s.db \\"
echo "       -provision 1 -provision-out /var/lib/nitr0s -provision-url https://<tu-nombre>.ts.net"
