# nitr0sd — servidor de nitr0S

Binario único en Go, SQLite en Go puro, **sin cgo**. Compila cruzado a la Raspberry Pi
desde cualquier sitio con dos variables de entorno, y **nunca se compila en la Pi**.

Implementa lo especificado en `../diseno/registro-de-operaciones-y-sincronizacion.md`.

## Estado

| Parte | Estado |
| --- | --- |
| Codificación canónica del registro de operaciones (marco, CRC-32, recuperación) | **hecha** |
| Motor de fusión por campo, con registro de conflictos | **hecha** |
| Compactación que conserva las `field_version` | **hecha** |
| Sobre, HMAC con rotación de claves, antirreplay por `envelope_seq` | **hecha** |
| Propuestas del asistente (no entran en el registro hasta aprobarse) | **hecha** |
| Marcador de tarjeta (tarjeta ajena no se fusiona) | **hecho** |
| Persistencia en SQLite: operaciones, conflictos, marcas de agua, eventos C3 | **hecha** |
| **Matriz de conflictos, los 15 casos** | **en verde** |
| Extremos HTTPS de las tres clases | pendiente |
| Consola web | pendiente |
| API de integración del asistente (ámbitos, auditoría) | pendiente, es el paso 3 |
| Planificador de repetición espaciada en el servidor | pendiente |

## Compilar y probar

```sh
go test ./...                 # 20 pruebas; la matriz de conflictos entera
go vet ./...
go run ./cmd/nitr0sd -db /tmp/nitr0s.db
```

## Compilación cruzada a la Pi

La arquitectura de la Pi es **Q12, sin resolver**: `uname -m` lo dice en un segundo.
`aarch64` → `arm64`; `armv7l` → `arm` con `GOARM=7`.

```sh
# Pi OS de 64 bits
CGO_ENABLED=0 GOOS=linux GOARCH=arm64 go build -trimpath -ldflags="-s -w" -o nitr0sd-arm64 ./cmd/nitr0sd

# Pi OS de 32 bits
CGO_ENABLED=0 GOOS=linux GOARCH=arm GOARM=7 go build -trimpath -ldflags="-s -w" -o nitr0sd-armv7 ./cmd/nitr0sd
```

Salen unos **5,7 MB**, estáticos, sin dependencias en la Pi. El presupuesto del brief
daba 15-30 MB para el binario, así que sobra de largo: Pi OS Lite más Tailscale más
esto queda muy por debajo de los 400 MB de 1 GB.

Copiar y ejecutar:

```sh
scp nitr0sd-arm64 pi@<host>:~/nitr0sd
ssh pi@<host> './nitr0sd -db ~/nitr0s.db'
```

## Versiones fijadas, y por qué

`go 1.24` y `modernc.org/sqlite v1.34.5`. Las versiones actuales de ese driver exigen
**Go 1.26**, lo que haría que `go build` se descargara un toolchain por su cuenta la
primera vez. Fijar el par evita esa sorpresa en una máquina nueva o sin red. Subirlo
es una decisión consciente, no algo que deba pasar solo.

## Lo que este servidor no hace, por diseño

* **No ordena por el reloj de pared.** Ordena `lamport`. El reloj de pared solo se muestra.
* **No usa caché de nonces.** El antirreplay es un contador monótono por aparato, que es
  correcto con un reloj arbitrariamente equivocado y no tiene tamaño.
* **No descarta en silencio una escritura perdedora.** Van a la tabla `conflicts`.
* **No mete las propuestas del asistente en el registro.** El registro contiene hechos.
