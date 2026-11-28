# Desplegar nitr0sd en la Raspberry Pi

La Pi arranca de una microSD que puedes meter en tu ordenador, así que todo lo
que sigue se hace desde ahí. No hace falta tener consola en la Pi.

## 1. Saber si la imagen es de 64 o de 32 bits, sin arrancar nada

Esto responde a **Q12** del brief, y se ve en la **partición de arranque**, que
es la única que tu ordenador lee sin herramientas raras (es FAT; la otra
partición es ext4 y Windows y macOS no la montan de serie).

Al meter la tarjeta aparece un volumen llamado **`bootfs`** (en versiones más
antiguas, `boot`). Dentro:

| Lo que ves | Qué significa | Qué binario necesitas |
| --- | --- | --- |
| `kernel8.img` | imagen de **64 bits** | `nitr0sd-arm64` |
| `arm_64bit=1` en `config.txt` | imagen de **64 bits** | `nitr0sd-arm64` |
| `kernel7.img` o `kernel7l.img` y **no** `kernel8.img` | imagen de **32 bits** | `nitr0sd-armv7` |

Una Pi 3 admite las dos. Si la tarjeta está sin grabar, elige la de **64 bits**:
el binario sale igual de pequeño y es lo que se mantiene mejor.

> Si prefieres no mirar: `desplegar.sh` detecta la arquitectura por SSH y
> compila la que toca, así que la pregunta se resuelve sola en cuanto la Pi
> esté en red.

## 2. Que la Pi arranque con SSH y WiFi, sin teclado ni pantalla

**Esto es lo único que no he podido verificar**: la documentación oficial de
Raspberry Pi está detrás de un desafío de Cloudflare que bloquea la lectura
automática, así que no la he leído y no te la copio de memoria como si lo
hubiera hecho.

Lo que sí es seguro: **Raspberry Pi Imager lo hace por ti**. Al grabar la
tarjeta, en los ajustes (el engranaje, o «Editar ajustes») puedes fijar nombre
de equipo, usuario y contraseña, activar SSH y meter la red WiFi con su país.
Si grabas la tarjeta así, el paso 2 está hecho y no hay que escribir ningún
fichero a mano.

Si la tarjeta **ya está grabada** y no sabes si lleva SSH activado, dímelo y lo
vemos: hay formas de comprobarlo mirando la partición de arranque, pero
prefiero confirmarte la sintaxis exacta antes de que escribas nada en la
tarjeta.

## 3. Desplegar

Con la Pi encendida y en red, desde tu ordenador y en la raíz del repositorio:

```sh
./despliegue/desplegar.sh pi@nitr0s.local
```

Detecta la arquitectura, compila cruzado con `CGO_ENABLED=0`, copia el binario
y lo instala como servicio de systemd. Son unos 5,7 MB estáticos, sin
dependencias en la Pi: no hace falta instalar Go ni nada ahí.

Para actualizar, el mismo comando. Es idempotente.

## 4. Primera vez: usuario y aparato

```sh
sudo -u nitr0s nitr0sd -db /var/lib/nitr0s/nitr0s.db -add-user aitor
sudo -u nitr0s nitr0sd -db /var/lib/nitr0s/nitr0s.db \
     -provision 1 -provision-out /var/lib/nitr0s -provision-url https://<tu-nombre>.ts.net
```

El primero pide la contraseña por la entrada estándar, así que no queda en el
historial. El segundo da de alta el aparato y escribe `provision.json` con
permisos 0600, que es el fichero que va a la **tarjeta del aparato** —esa es la
otra tarjeta, la del Xteink, que todavía no sirve de nada porque no hay
firmware.

## 5. Exponerlo con Tailscale Funnel

El servicio escucha **solo en loopback** a propósito: quien lo expone es
Funnel, y el autenticador es el HMAC y no el TLS, porque el TLS termina en
Funnel y no en la Pi.

```sh
tailscale funnel --bg 8080
tailscale funnel status
```

De ahí sale el nombre `https://<algo>.ts.net` que va en `-provision-url`.

## Qué hace el servicio

* Escucha en `127.0.0.1:8080`: API y consola en el mismo puerto.
* Corre como usuario `nitr0s`, sin privilegios, con `ProtectSystem=strict` y
  acceso de escritura solo a `/var/lib/nitr0s`.
* `MemoryMax=256M`: una Pi 3 tiene 1 GB, así que si algo se desmadra lo paga el
  servicio y no el sistema.
* Se reinicia solo si falla, con 5 segundos de espera.
