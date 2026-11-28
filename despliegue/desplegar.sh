#!/bin/sh
# Compila y despliega nitr0sd en la Pi. Se ejecuta EN TU ORDENADOR.
#
#   ./desplegar.sh pi@nitr0s.local
#
# Detecta la arquitectura de la Pi por SSH y compila la que toca, así que
# resuelve Q12 por el camino en vez de preguntarla.
set -eu

DESTINO="${1:?uso: ./desplegar.sh usuario@host}"
RAIZ="$(cd "$(dirname "$0")/.." && pwd)"

echo "== arquitectura de la Pi =="
ARCO="$(ssh "$DESTINO" 'uname -m')"
case "$ARCO" in
    aarch64) GOARCH=arm64; GOARM=;   SUF=arm64 ;;
    armv7l)  GOARCH=arm;   GOARM=7;  SUF=armv7 ;;
    armv6l)  GOARCH=arm;   GOARM=6;  SUF=armv6 ;;
    *) echo "arquitectura no contemplada: $ARCO" >&2; exit 1 ;;
esac
echo "$ARCO -> GOARCH=$GOARCH${GOARM:+ GOARM=$GOARM}"

echo "== compilando =="
cd "$RAIZ/servidor"
CGO_ENABLED=0 GOOS=linux GOARCH="$GOARCH" GOARM="${GOARM:-}" \
    go build -trimpath -ldflags="-s -w" -o "/tmp/nitr0sd-$SUF" ./cmd/nitr0sd
ls -lh "/tmp/nitr0sd-$SUF" | awk '{print $5, $NF}'

echo "== copiando =="
scp "/tmp/nitr0sd-$SUF" "$RAIZ/despliegue/nitr0sd.service" "$RAIZ/despliegue/instalar.sh" "$DESTINO:~/"

echo "== instalando =="
ssh -t "$DESTINO" "chmod +x ~/instalar.sh && sudo ~/instalar.sh ~/nitr0sd-$SUF"
