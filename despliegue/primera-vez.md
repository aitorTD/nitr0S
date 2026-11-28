# De tarjeta en blanco a servidor funcionando

Camino completo, en orden. La tarjeta se graba entera; lo que tuviera se pierde.

Cada paso dice **qué tiene que pasar para darlo por bueno**. Si un paso no
cumple su comprobación, para ahí: seguir adelante con un paso a medias es lo
que convierte una tarde en un fin de semana.

---

## 1. Grabar la tarjeta

**Imagen: Raspberry Pi OS Lite, 64 bits.**

* **Lite**, sin escritorio: la Pi no tiene pantalla y el presupuesto de recursos
  del brief cuenta con Lite (80-150 MB), no con el escritorio.
* **64 bits**: la Pi 3 lo admite, y el binario del servidor sale igual de
  pequeño. Así además queda fijado `nitr0sd-arm64` y Q12 deja de estar abierta.

En **Raspberry Pi Imager**, antes de grabar, entra en los ajustes (el engranaje,
o el botón que pregunta si quieres personalizar) y deja configurado:

| Ajuste | Valor |
| --- | --- |
| Nombre de equipo | `nitr0s` → luego responderá como `nitr0s.local` |
| Usuario y contraseña | el que quieras; apúntalo |
| WiFi | tu SSID, su clave, y **el país correcto** (`ES`) |
| Zona horaria y teclado | las tuyas |
| **Activar SSH** | sí, con contraseña o con clave pública |

> El país del WiFi no es un detalle: sin él, la radio puede no levantar.

**No he podido verificar esta pantalla contra la documentación oficial**, porque
el sitio de Raspberry Pi bloquea la lectura automática. Los nombres de los
campos pueden variar entre versiones de Imager; lo que no varía es la lista de
cosas que tienen que quedar configuradas.

**Comprobación:** al terminar, saca y vuelve a meter la tarjeta. Tiene que
aparecer un volumen `bootfs`, y dentro un `config.txt`. Si además ves
`kernel8.img`, has grabado la de 64 bits, que es la que quieres.

---

## 2. Arrancar y entrar

Mete la tarjeta en la Pi, enchúfala y espera un par de minutos: el primer
arranque hace trabajo extra.

```sh
ssh <usuario>@nitr0s.local
```

**Comprobación:** entra sin pedir nada raro. Comprueba de paso la arquitectura:

```sh
uname -m          # aarch64 si grabaste la de 64 bits
free -h           # ~1 GB en una Pi 3
```

Si `nitr0s.local` no resuelve, busca la IP en tu router y usa la IP. Si el SSH
no entra, el problema está en el paso 1 y conviene volver a grabar antes que
investigar: grabar son cinco minutos.

---

## 3. Poner la Pi a punto

```sh
sudo apt update && sudo apt full-upgrade -y

# Menos escrituras en la tarjeta, que es lo que mata a las Pi.
sudo apt install -y log2ram || true     # si no está en los repos, no pasa nada
sudo systemctl disable --now triggerhappy 2>/dev/null || true

# Swap fuera: con 1 GB y SQLite en la tarjeta, hace más daño que bien.
sudo dphys-swapfile swapoff && sudo systemctl disable --now dphys-swapfile
```

Las dos últimas salen del punto **B11** del brief, sobre que ninguno de los dos
lados tiene copia de seguridad y el lado con el almacenamiento más débil es el
que manda.

**Comprobación:** `free -h` ya no muestra swap.

---

## 4. Tailscale

```sh
curl -fsSL https://tailscale.com/install.sh | sh
sudo tailscale up
```

Te dará un enlace que tienes que abrir en tu navegador para autorizar la Pi.

```sh
tailscale ip -4          # su dirección en tu red privada
```

**Comprobación:** desde tu ordenador, con Tailscale también instalado,
`ssh <usuario>@<la-ip-de-tailscale>` entra. A partir de aquí la Pi es
alcanzable desde fuera de casa sin abrir ni un puerto del router.

---

## 5. Desplegar el servidor

Desde tu ordenador, en la raíz del repositorio:

```sh
./despliegue/desplegar.sh <usuario>@nitr0s.local
```

Detecta la arquitectura, compila cruzado y lo instala como servicio.

**Comprobación:** el propio guion termina enseñando la respuesta de `/hello`.
Tiene que decir algo así:

```json
{"server_time":1764300000,"proto_versions":[1],"dataset_id":2001,"update_path":"/update"}
```

---

## 6. Usuario de la consola

```sh
ssh <usuario>@nitr0s.local
sudo -u nitr0s nitr0sd -db /var/lib/nitr0s/nitr0s.db -add-user aitor
```

Pide la contraseña por la entrada estándar, así que no queda en el historial.
Mínimo diez caracteres.

**Comprobación:** desde tu ordenador, con un túnel,

```sh
ssh -L 8080:127.0.0.1:8080 <usuario>@nitr0s.local
```

y abre `http://127.0.0.1:8080/` en el navegador. Tienes que ver la pantalla de
entrar, y pasar al panel con el usuario que acabas de crear.

---

## 7. Funnel, cuando quieras llegar desde fuera

```sh
sudo tailscale funnel --bg 8080
tailscale funnel status
```

De ahí sale el nombre `https://algo.ts.net`. **Apúntalo**: va en
`-provision-url` cuando des de alta el aparato, y es el nombre que el firmware
llevará dentro.

> El plan B del brief, DuckDNS, garantiza que ese nombre cambie al menos una
> vez. Por eso el reaprovisionamiento por tarjeta tiene que funcionar, y por eso
> el aparato guarda el nombre en NVS y no compilado dentro.

---

## Lo que NO toca hacer todavía

Dar de alta el aparato con `-provision` y escribir `provision.json`. Esa es la
tarjeta **del Xteink**, no la de la Pi, y no sirve de nada hasta que exista
firmware. Cuando llegue el aparato, ese será el primer paso.
