#!/bin/sh
# Prueba de extremo a extremo: consola -> servidor -> aparato.
#
# Levanta un servidor de verdad, da de alta un aparato, escribe ítems DESDE LA
# CONSOLA WEB como lo haría una persona, y hace que el simulador los baje y los
# fusione, con el sobre firmado y el antirreplay de por medio.
#
# Es la parte de sincronización de la Puerta 1: las dos implementaciones
# hablando por HTTP, sin ningún atajo.
set -eu

RAIZ="$(cd "$(dirname "$0")/../.." && pwd)"
TMP="$(mktemp -d)"
PUERTO="${PUERTO:-8099}"
CLAVE="contrasena-de-prueba"
trap 'kill ${PID:-0} 2>/dev/null || true; rm -rf "$TMP"' EXIT
# no_proxy en el entorno en vez de --noproxy '*' en una variable: dentro de una
# variable sin comillas, el asterisco lo expandiría el shell.
no_proxy='*'
export no_proxy
CURL="curl -sf"

echo "== compilando el servidor =="
cd "$RAIZ/servidor" && GOTOOLCHAIN=local go build -o "$TMP/nitr0sd" ./cmd/nitr0sd

echo "== dando de alta el aparato 208 y el usuario de la consola =="
"$TMP/nitr0sd" -db "$TMP/n.db" -provision 208 -provision-out "$TMP" \
    -provision-url "http://127.0.0.1:$PUERTO" >/dev/null
printf '%s\n' "$CLAVE" | "$TMP/nitr0sd" -db "$TMP/n.db" -add-user aitor >/dev/null

echo "== arrancando el servidor =="
"$TMP/nitr0sd" -db "$TMP/n.db" -listen "127.0.0.1:$PUERTO" -device-id 208 >"$TMP/srv.log" 2>&1 &
PID=$!
i=0
while [ $i -lt 40 ]; do
    $CURL -o /dev/null "http://127.0.0.1:$PUERTO/hello" 2>/dev/null && break
    i=$((i+1))
done
$CURL -o /dev/null "http://127.0.0.1:$PUERTO/hello" || { echo "el servidor no arrancó"; cat "$TMP/srv.log"; exit 1; }

echo "== la consola escribe tres ítems, como una persona =="
$CURL -c "$TMP/ck" -b "$TMP/ck" -o /dev/null \
    -d "usuario=aitor&clave=$CLAVE" "http://127.0.0.1:$PUERTO/login"
for t in "Leche entera" "Pan de centeno" "Pilas AAA"; do
    $CURL -c "$TMP/ck" -b "$TMP/ck" -o /dev/null \
        --data-urlencode "texto=$t" "http://127.0.0.1:$PUERTO/listas/crear"
done
echo "   escritos desde la consola: Leche entera, Pan de centeno, Pilas AAA"

echo "== el aparato sincroniza =="
cd "$RAIZ/firmware"
./nitr0sim . -sinc "$TMP/provision.json" 127.0.0.1 "$PUERTO"

echo
echo "== y la consola ve lo que escribió el aparato =="
# La otra dirección: lo que el aparato subió tiene que aparecer en la web.
PAG="$($CURL -c "$TMP/ck" -b "$TMP/ck" "http://127.0.0.1:$PUERTO/listas")"
FALTA=0
for t in "Pilas de" "Caf" "Leche entera"; do
    if printf '%s' "$PAG" | grep -q "$t"; then
        echo "   la consola muestra: $t..."
    else
        echo "   FALTA en la consola: $t"
        FALTA=1
    fi
done
[ "$FALTA" = 0 ] || { echo "FALLO: la subida del aparato no llegó a la consola"; exit 1; }

echo
echo "camino completo correcto: consola -> servidor -> aparato -> servidor -> consola"
