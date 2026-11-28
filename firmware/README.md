# Núcleo y simulador de nitr0S

El núcleo en C que acabará en el aparato, y un simulador de escritorio que lo
ejecuta tal cual y lo saca a ficheros de 800×480 y 1 bit.

Que sea **el mismo código** es el punto entero: §12.3 del brief pide revisar las
propuestas visuales «con el mismo renderizador que acabará en el aparato, no con
una maqueta que luego no se parece».

## Qué hay

| Parte | Estado |
| --- | --- |
| `nucleo/gfx.c` — lienzo de 1 bit, recorte, invertir | hecho |
| `nucleo/vista.c` — árbol de vista, conjunto cerrado de 12 nodos, validación | hecho |
| `nucleo/fuente.c` — UTF-8, glifos de mapa de bits, recorte con elipsis | hecho |
| `nucleo/render.c` — árbol de vista → lienzo, con la maqueta como datos | hecho |
| `nucleo/maquetas.c` — **tres propuestas** para la fase visual | hecho, **ninguna aprobada** |
| `sim/` — simulador y salida PBM | hecho |
| `nucleo/lanzador.c` — pila de pantallas, presupuestos, cuarentena, arena | hecho |
| `apps/` — **las tres apps de v1**: Listas, Tests y Estado | hechas contra un almacén de mentira |
| `sim/guion.c` — ejecuta el lanzador con un guion de pulsaciones | hecho |
| `pruebas/` — 91 comprobaciones, más la cruzada y la de extremo a extremo | en verde |
| `nucleo/opfmt.c` — codificación canónica, **los mismos bytes que el servidor** | hecha |
| `nucleo/fusion.c` — motor de fusión con las mismas reglas que el servidor | hecho |
| **Prueba cruzada**: C y Go fusionan el mismo registro y convergen | **en verde** |
| `nucleo/almacen.c` — la API de datos de las apps, sobre el registro | hecha |
| **Las apps corren sobre el almacén de verdad**, no sobre uno de mentira | hecho |
| `nucleo/sha256.c` — SHA-256 y HMAC, contra los vectores de FIPS y RFC 4231 | hecho |
| `nucleo/sinc.c` — el sobre y el ciclo de sincronización | hecho |
| **Sincronización de extremo a extremo contra el servidor de verdad** | **en verde** |
| Árbitro de sueño | pendiente |
| Capa de abstracción de hardware, panel | pendiente, necesita el aparato |
| Capa de abstracción de hardware, panel, sueño | pendiente, necesita el aparato |

## Usar

```sh
make                     # compila todo y pasa las pruebas y la cruzada
make e2e                 # levanta un servidor de verdad y sincroniza contra él
./nitr0sim .             # un .pbm por maqueta y pantalla, para la fase visual
./nitr0sim . -guion      # ejecuta las apps de verdad con un guion de pulsaciones
go run ../herramientas/pbm2png -escala 2 *.pbm
```

Para regenerar el material de la prueba cruzada, desde `servidor/`:

```sh
NITR0S_FIXTURE=$PWD/../firmware/pruebas/datos go test ./internal/merge/ -run Fixture
```

## La tipografía es un marcador de posición

`fuente_cuerpo.c` y `fuente_titulo.c` están **generados** por
`herramientas/genfuente` a partir de DejaVu, y **no son una propuesta visual**:
la familia, los pesos y los tamaños son decisiones del autor (§12.2). Cambiarlos
es regenerar esos ficheros, sin tocar el renderizador:

```sh
go run ../herramientas/genfuente -ttf <tu-fuente.ttf> -px 18 \
  -nombre n0_fuente_cuerpo -salida nucleo/fuente_cuerpo.c
```

Cuesta unos 4 KB de flash a 18 px y 6 KB a 26 px, con 119 glifos que incluyen
la cobertura obligatoria del español: acentos, `ñ`, `¿` y `¡`.

## Lo que el núcleo exige y no se puede saltar

Son las propiedades de la sección 13 del brief convertidas en comprobaciones, y
el renderizador **se niega a pintar** un árbol que las incumpla:

* Un estado vacío sin «qué hacer» no es un estado vacío diseñado.
* Un error sin «qué puede hacer el usuario», tampoco.
* Ninguna pantalla puede presentar cero acciones posibles.
* Siempre hay vuelta atrás.
* El árbol tiene un tope de 256 nodos y pasarse se detecta, no corrompe memoria.

## Lo que el lanzador garantiza

* **Un error devuelto y un plazo agotado se tratan igual**: deja la app, pinta
  una pantalla legible con el código y la app culpable, y el sistema sigue.
* **Nunca se devuelve un lienzo sin pintar.** Si la app falla, o devuelve un
  árbol inválido, o la pila está vacía, se pinta la pantalla de error, que
  cumple las mismas reglas que el núcleo exige a las apps.
* **Cuarentena a los dos fallos consecutivos**, y «consecutivos» significa sin
  una sesión terminada limpiamente en medio. La cuarentena de una app no toca
  a las demás.
* **Desenrollar no pregunta y no puede fallar**: vacía la pila y libera los
  cerrojos de vigilia, que son del sistema y nunca de la app. Un fallo de app
  no puede impedir dormir.
* **La arena es del sistema** y se tira entera al abandonar la app, que es lo
  que hace que matarla sea recuperable en vez de una fuga.
* **Una capacidad desconocida o un `api_required` fuera de rango se rechazan al
  CARGAR**, no al llamar.

## Por qué existe la prueba cruzada

El servidor fusiona en Go y el aparato en C. Son **dos implementaciones
distintas de las mismas reglas**, y nada obliga a que coincidan salvo que se
compruebe. `make cruzado` coge el registro que generó el servidor, lo fusiona
con el núcleo, y exige que el volcado del estado sea **idéntico carácter por
carácter**, conflictos y motivos incluidos.

El registro de prueba lleva a propósito los casos que separan una
implementación correcta de una que lo parece: empates de versión que gana el
aparato, un borrado que sobrevive a que se edite otro campo, una resurrección
explícita, una creación del asistente que no puede perder, ediciones sucesivas
del mismo autor que **no** son conflictos, y texto con acentos y `ñ`.

Si esta prueba se rompe, la sincronización está rota de una forma que en
producción no se nota hasta meses después, cuando ya hay historial que perder.

## Presupuesto de memoria

El simulador corre en un portátil, así que nada impide escribir estructuras
enormes sin enterarse. Por eso hay un tope comprobado:

```
memoria: fusión 36912 B, lanzador 856 B, vista 10248 B, framebuffer 48000 B
```

Existe porque ya pasó: el estado fusionado guardaba el valor de cada campo en
línea, con el tope de 4 KB que admite una operación, y ocupaba **3,6 MB** —
imposible en un ESP32-S3, que tiene 512 KB de SRAM. Lo encontró un segfault en
las pruebas, no una revisión. Ahora los valores viven en un almacén aparte que
pone quien llama, y el tope se comprueba en cada ejecución.

## La prueba de extremo a extremo

`make e2e` hace el camino entero, sin atajos:

1. Compila y arranca el servidor en Go con una base de datos limpia.
2. Da de alta un aparato y escribe su `provision.json`.
3. **Entra en la consola web y crea tres ítems**, con `curl`, como lo haría una
   persona con un navegador.
4. El simulador lee ese `provision.json`, crea dos ítems propios, y sincroniza:
   sube los suyos y baja los de la consola, con el sobre firmado con HMAC.
5. Comprueba que un segundo ciclo no duplica nada.
6. Comprueba que un sobre con firma mala da **401 con la hora del servidor
   dentro**, que es el camino de recuperación por desvío de reloj.
7. Y vuelve a la consola a comprobar que **lo que escribió el aparato aparece
   allí**.

Es decir: consola → servidor → aparato → servidor → consola, con las dos
implementaciones hablando por HTTP.
