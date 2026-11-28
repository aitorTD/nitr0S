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
| **Extremos de las tres clases** (`/v1/sync/ops`, `/v1/sync/events`, `/v1/content/{hash}`) | **hechos** (C1 todavía sin almacén de contenido) |
| **Contrato congelado sin versión en la ruta** (`/hello`, `/update`) | **hecho** |
| **Autenticación por HMAC con rotación, y 401 con la hora del servidor** | **hecha** |
| **Antirreplay persistido**, que sobrevive a reiniciar el servidor | **hecho** |
| **Aprovisionamiento**: alta de aparato y `provision.json` para la microSD | **hecho** |
| **Consola web**: panel, listas, conflictos, validador de bancos | **hecha**, deliberadamente sin estilo (ver abajo) |
| **Autenticación humana** (PBKDF2 de la biblioteca estándar, sesiones) | **hecha** |
| **Los dos estados de entrega**: guardado en la Pi / entregado al aparato | **hechos** |
| Almacén de contenido direccionado por hash (C1) e imagen de OTA | pendiente |
| API de integración del asistente (ámbitos, auditoría) | pendiente, es el paso 3 |
| Planificador de repetición espaciada en el servidor | pendiente |

## Compilar y probar

```sh
go test ./...                 # 47 pruebas; la matriz de conflictos entera
go vet ./...
```

## Arrancar

```sh
# dar de alta el aparato y escribir el fichero que va a la microSD (0600)
go run ./cmd/nitr0sd -db ~/nitr0s.db -provision 1 -provision-url https://<tu-nombre>.ts.net

# crear el usuario de la consola (la contraseña se lee de la entrada estándar,
# así que no queda en el historial del shell ni en la lista de procesos)
go run ./cmd/nitr0sd -db ~/nitr0s.db -add-user aitor

# servir (API + consola en el mismo puerto)
go run ./cmd/nitr0sd -db ~/nitr0s.db -listen 127.0.0.1:8080 -device-id 1

# ver qué hay dentro sin servir
go run ./cmd/nitr0sd -db ~/nitr0s.db -status
```

Escucha **en claro**: el TLS lo pone Tailscale Funnel por delante, y por eso el
autenticador es el HMAC y no el TLS. `provision.json` y `*.db` están en `.gitignore`.

Comprobación rápida de que está vivo:

```sh
curl -s http://127.0.0.1:8080/hello
# {"server_time":...,"proto_versions":[1],"dataset_id":2001,"update_path":"/update"}
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
* **No versiona `/hello` ni `/update`.** Son el contrato congelado: un cambio de protocolo que
  los rompiera sería irrecuperable sin USB, y este aparato puede no tener USB. `/v1/hello` da 404
  a propósito, y hay una prueba que lo exige.
* **No distingue un aparato desconocido de una firma mala.** Los dos dan el mismo 401, con la hora
  del servidor dentro.

## Por qué la consola no tiene estilo

Es deliberado, no provisionalidad perezosa. La sección 12.3 del brief fija el orden
de la fase visual: primero el inventario de pantallas, después dos o tres propuestas
de la **pantalla de lista del aparato**, que es la que manda porque las tres apps son
listas, y **«la consola se hace después, tomando los mismos tokens»**.

Inventar aquí una tipografía, unos espaciados o unos colores sería decidir por omisión
lo que la corrección 8 del brief reserva explícitamente al autor. Así que la consola
funciona entera, con HTML semántico y sin una sola línea de CSS, y se ve como la ve el
navegador. Cuando existan los tokens, se le pone una hoja de estilo y el marcado ya
está estructurado para recibirla.
