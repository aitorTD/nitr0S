# API del núcleo — especificación de la costura `app_t`

* **Autor:** aitorTD
* **Fecha:** 2026-11-28
* **Estado:** especificación de diseño, no implementación. Nada de aquí se ha compilado, ejecutado ni medido; los presupuestos de reloj y los tamaños son objetivos de diseño, no mediciones.
* **Origen:** paso 4c de la cola de arranque de `concepts_brief.md`, y respuesta a B9, que exigía esta cabecera **antes de la primera app**.
* **Hermanos:** `manifiesto-y-paquete-de-app.md` (4a), `registro-de-operaciones-y-sincronizacion.md` (4b), `registro-de-repasos-y-planificador.md`.

---

## 0. Qué fija y qué no

**Fija:** la tabla `app_t`, los ganchos y sus presupuestos, el aislamiento, las acciones semánticas, el conjunto cerrado de nodos de vista, las reglas de pintado que son del núcleo y no de las apps, la tabla de servicios, la API de `store` con sus cuotas, el esquema de errores y el versionado de la API.

**No fija, y es deliberado:** tipografía, tamaños concretos, iconos, espaciados, disposiciones ni temas. Este documento define **qué nodos existen y qué significan**; cómo se ven es la fase visual de la sección 12 del brief. La frontera está en que un nodo describe *intención* —«una fila seleccionable con un estado»— y nunca *apariencia* —«una fila de 24 píxeles con la fuente mediana».

---

## 1. La costura

```c
// nitr0s_app.h — el contrato central del proyecto.
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define NITR0S_API_VERSION 1   // la que ofrece este núcleo
#define NITR0S_API_FLOOR   1   // la más vieja que sigue cargando

typedef enum {
  N0_OK = 0,
  N0_ERR_ARG, N0_ERR_STATE, N0_ERR_QUOTA, N0_ERR_NOT_FOUND,
  N0_ERR_NO_CAP,        // capacidad no concedida: solo puede verse en el simulador
  N0_ERR_BUDGET,        // el gancho agotó su presupuesto de reloj
  N0_ERR_STORE_IO, N0_ERR_SCHEMA, N0_ERR_INTERNAL
} n0_err_t;

typedef struct n0_ctx   n0_ctx_t;    // opaco: servicios concedidos + arena + descriptores
typedef struct n0_view  n0_view_t;   // opaco: constructor del árbol de vista, propiedad del núcleo

typedef struct {
  uint16_t  api_required;
  const char *id;

  n0_err_t (*on_start)(n0_ctx_t *, bool cold);
  n0_err_t (*on_event)(n0_ctx_t *, const struct n0_event *);
  n0_err_t (*on_resume)(n0_ctx_t *);
  n0_err_t (*on_dataset_changed)(n0_ctx_t *, uint32_t dataset_id);
  n0_err_t (*on_stop)(n0_ctx_t *);
  n0_err_t (*render)(n0_ctx_t *, n0_view_t *);
} app_t;
```

Seis funciones y dos tipos opacos. **No hay ninguna función de dibujo, ninguna de red y ninguna de fichero**, y eso no es una omisión: es §5.1 del documento de manifiesto expresado en la cabecera.

---

## 2. Ganchos, presupuestos y qué pasa al pasarse

| Gancho | Cuándo | Presupuesto objetivo |
| --- | --- | --- |
| `on_start` | Al lanzarse. `cold` distingue arranque de la vuelta desde deep sleep | 50 ms |
| `on_event` | Una acción semántica o un evento del sistema | 50 ms |
| `on_resume` | Vuelta de light sleep o de otra app | 20 ms |
| `on_dataset_changed` | Un conjunto de datos suyo cambió, normalmente tras sincronizar | 50 ms |
| `on_stop` | Antes de abandonarla | 20 ms |
| `render` | Cuando el núcleo necesita el árbol de vista | **10 ms**, y es el más estricto |

`render` es el más estricto por una razón concreta: aunque **no está en el camino crítico del refresco** —el núcleo compara árboles y es él quien habla con el panel—, sí está en el camino de la respuesta percibida, que es el presupuesto de menos de 25 ms de software desde el flanco del botón.

**Los presupuestos son cifras de diseño, no medidas.** Se instrumentan desde el primer día y se ajustan con datos; lo que no cambia es que exista un presupuesto por gancho.

Al pasarse: el lanzador trata **un error devuelto y un plazo agotado exactamente igual**. Deja la app, pinta una pantalla legible con el código y la app culpable, escribe el registro y **el sistema sigue**.

---

## 3. Aislamiento, y por qué la regla de los descriptores existe

Una app corre en **su propia tarea**, con prioridad por debajo del árbitro de sueño y de la tarea de entrada. Eso es lo que permite que el núcleo recupere el control de una app que no vuelve.

Y aquí es donde encaja una regla del brief que parecía de higiene y es estructural: **todo recurso del sistema se entrega por descriptor, con cierre a cargo del sistema, nunca como puntero crudo**; y **la memoria de la app es una arena que el sistema posee y libera entera**. El motivo es este: matar la tarea de una app solo es recuperable si al matarla no se filtra nada. Con descriptores y arena, abandonar una app es cerrar sus descriptores y tirar su arena. Sin eso, cada fallo de app dejaría basura hasta el siguiente reinicio, y un bucle infinito pasaría de contenible a fatal.

Tres consecuencias que se escriben aquí para que no se descubran tarde:

* **`render` no asigna memoria.** Recibe un `n0_view_t *` que es un constructor sobre un **buffer del núcleo**, con un tope de nodos. Así el árbol no plantea ninguna pregunta de propiedad —el núcleo no tiene que copiar ni liberar nada de la app— y `render` no puede quedarse sin memoria.
* **Los cerrojos de vigilia son del sistema.** Una app puede pedir «mantén despierto para esto», pero el cerrojo lo posee el sistema y **se libera cuando el lanzador la abandona**. Una app nunca lo retiene. Esto es lo que hace cierto el invariante de que un fallo de app **nunca impide dormir**.
* **El bucle infinito tiene un límite honesto.** Un presupuesto cooperativo no interrumpe a nadie: lo que contiene el bucle es que la tarea sea matable y que el perro guardián de tarea exista. Si incluso así se llega al reinicio, el requisito es el del brief: **el 100% de los reinicios deja en memoria RTC la causa y la app culpable**. «Sin fallos» no es alcanzable; «sin fallos indiagnosticables» sí.

**Cuarentena: dos fallos consecutivos de la misma app**, con reactivación manual. Es el umbral único de todo el proyecto. Y **el fallo del gancho de una app no aborta el de las demás**.

---

## 4. Entrada: acciones semánticas

```c
typedef enum {
  N0_ACT_NEXT, N0_ACT_PREV, N0_ACT_SELECT, N0_ACT_BACK, N0_ACT_MENU,
  N0_ACT_1, N0_ACT_2, N0_ACT_3, N0_ACT_4,          // reservadas
  N0_ACT_LONG_SELECT, N0_ACT_LONG_BACK
} n0_action_t;

struct n0_event {
  uint8_t  kind;        // acción, conjunto de datos, batería, reloj, sincronización
  uint8_t  action;      // n0_action_t
  uint8_t  repeat;      // pulsaciones agrupadas: 5 abajo rápidas llegan como repeat=5
  uint32_t arg;
};
```

**Una app nunca ve un código de botón.** El mapeo físico es propiedad del módulo de entrada y se puede remapear. El motivo no es elegancia: **el número y la posición de los botones están sin confirmar** (Q7), y usar códigos crudos congelaría un hecho de hardware no verificado dentro de la ABI de las apps.

El campo `repeat` es la agrupación hecha explícita, y existe por la regla de §6: **antirrebote y agrupación, nunca descartar**.

---

## 5. El árbol de vista: conjunto cerrado de nodos

`render` devuelve una **descripción**, no dibujo. El núcleo la compara con la anterior y de esa comparación sale la diferencia de rectángulos sucios, que es exactamente lo que necesita el refresco parcial. Y al ser un conjunto **cerrado** de tipos de nodo en vez de un conjunto abierto de llamadas de dibujo, la API es un contrato versionable, y el mismo árbol lo pueden pintar **dos renderizadores**: C para el e-ink y HTML para la consola. Eso es lo que responde a «compartir el lenguaje visual sin construir un emulador».

| Nodo | Significa | Campos |
| --- | --- | --- |
| `screen` | Raíz. Una pantalla | título, nodo de contenido, barra de acciones |
| `list` | Lista paginada y seleccionable. **El nodo que manda**, porque las tres apps de v1 son listas | filas, índice seleccionado, página |
| `row` | Una fila de una lista | texto, texto secundario, marca de estado |
| `kv` | Etiqueta y valor | etiqueta, valor, unidad |
| `text` | Bloque de texto **paginado** | contenido, página |
| `prompt` | Una pregunta con opciones fijas | enunciado, opciones, índice seleccionado |
| `empty` | Estado vacío | qué falta, **qué hacer para arreglarlo** |
| `busy` | Operación en marcha, con principio y fin | qué se está haciendo, paso actual de total |
| `error` | Fallo | código, frase humana, **qué puede hacer el usuario** |
| `badge` | Marca pequeña dentro de una fila | estado enumerado |

Diez nodos, y dos elecciones que no son de aspecto sino de política:

* **`empty` y `error` son tipos de nodo, no pantallas que cada app se dibuje.** Esto convierte «todas las apps tienen un estado vacío diseñado» y «los errores llevan frase humana y qué hacer» de aspiración de la sección 13 a **algo que el núcleo puede exigir**: un `empty` sin el campo «qué hacer» no compila el árbol. El cuidado deja de depender de lo que cada sesión recuerde.
* **No hay nodo de desplazamiento.** `list` y `text` tienen página, no posición de scroll, porque **se pagina, no se hace scroll**: el desplazamiento píxel a píxel en e-ink es un gasto de refresco sin beneficio.

**Lo que no existe en el conjunto, a propósito:** imágenes arbitrarias, lienzo libre, animación, nodo de posición absoluta y cualquier campo de tamaño en píxeles, de fuente o de color. Si un nodo pudiera decir «fuente mediana», la fase visual estaría decidida por omisión en este documento, que es justamente lo que la corrección 8 prohíbe.

---

## 6. Reglas de pintado que son del núcleo

Son las técnicas que hacen que un e-ink sin refresco parcial parezca responder, y aquí está el motivo de que vivan en el núcleo: **si fueran costumbre de cada app, bastaría una app descuidada para que el aparato pareciera roto, y todas serían escritas por sesiones de IA sin memoria compartida.**

* **Pintar y luego trabajar.** El acuse visual de una pulsación se manda al panel **antes** de ejecutar la escritura del manejador. Es una regla del núcleo, no una costumbre.
* **El acuse cabe en la región más pequeña posible.** Una barra de selección invertida es todo el truco.
* **Antirrebote y agrupación, nunca descartar.** Cinco pulsaciones rápidas son un salto de cinco filas y **un** repintado. Descartar pulsaciones es la primera causa de que un e-ink parezca roto, y una pulsación ignorada es indistinguible de un aparato muerto porque la pantalla es el único canal de respuesta.
* **Nada de animaciones y nada de desplazamiento píxel a píxel.**
* **Pre-renderizar la página siguiente** cuando haya margen: 48 KB cada una, caben docenas.
* **Ninguna pantalla con cero acciones posibles**, y siempre hay vuelta atrás. El núcleo lo comprueba sobre el árbol: un `screen` cuya barra de acciones quede vacía es un error de la app, no una pantalla.
* **Plan B escrito por adelantado si no hay refresco parcial: selección directa**, numerando las filas visibles y mapeándolas sobre los botones. Cambia el mapeo de entrada y la barra de acciones, **no el árbol de vista**, que es precisamente el beneficio de que la app describa intención.

---

## 7. La tabla de servicios

```c
typedef struct {
  // store: siempre que haya capacidad "store"
  n0_err_t (*get)(n0_ctx_t *, uint32_t ds, const uint8_t id[16], uint16_t field, void *out, uint16_t *len);
  n0_err_t (*set)(n0_ctx_t *, uint32_t ds, const uint8_t id[16], uint16_t field, const void *v, uint16_t len);
  n0_err_t (*create)(n0_ctx_t *, uint32_t ds, uint8_t id_out[16]);
  n0_err_t (*del)(n0_ctx_t *, uint32_t ds, const uint8_t id[16]);
  n0_err_t (*append)(n0_ctx_t *, uint32_t ds, const void *rec, uint16_t len);   // solo-añadir
  n0_err_t (*iter)(n0_ctx_t *, uint32_t ds, n0_iter_t *out);

  // cada uno presente solo si su capacidad fue concedida; si no, NULO
  n0_err_t (*sync_status)(n0_ctx_t *, n0_sync_state_t *out);
  n0_err_t (*sync_force)(n0_ctx_t *);
  n0_err_t (*power_read)(n0_ctx_t *, n0_power_t *out);
  n0_err_t (*device_control)(n0_ctx_t *, n0_dev_cmd_t cmd);
  n0_err_t (*radio_request)(n0_ctx_t *, uint16_t seconds);
  n0_err_t (*update_start)(n0_ctx_t *);
  n0_err_t (*diag_read)(n0_ctx_t *, n0_diag_t *out);
  n0_err_t (*time_status)(n0_ctx_t *, n0_time_t *out);
} n0_services_t;
```

**Una capacidad no concedida llega como puntero nulo, comprobado al cargar, no fallando al llamarse.** El núcleo verifica al cargar que la app no referencia ningún servicio que no tenga concedido; en el aparato, por tanto, `N0_ERR_NO_CAP` no debería poder ocurrir nunca, y si ocurre es un fallo del núcleo. Donde sí se ve es en el simulador, que es donde se escribe el código.

`radio_request` pide, no concede: **la decisión sigue siendo del árbitro de sueño**, que puede negarla y cuyo invariante manda sobre cualquier app.

Y lo que **no** está en la tabla, repetido porque es el error más tentador: **nada de sincronizar.** Las apps escriben en `store`, `store` añade al registro de operaciones, `sync` lo drena según su calendario, y las apps se **suscriben** a «este conjunto de datos cambió» vía `on_dataset_changed`. Exponer la sincronización invertiría la dependencia y destruiría en silencio el funcionamiento sin red, porque una app que se olvidara de llamarla perdería datos sin avisar.

---

## 8. `store`: garantías y cuotas

**Garantías.** Una escritura que devuelve `N0_OK` está en el registro de salida, con marco y CRC, y sobrevive a un corte de corriente. No está necesariamente **sincronizada**, y la app no tiene forma de preguntarlo porque no debe: eso lo cuenta la consola con sus dos estados de entrega, «guardado en la Pi» y «entregado al aparato».

`create` acuña el identificador según 4b §2.1, el par `(creator_id, creator_seq)`. La app no lo elige, lo recibe. `del` escribe `deleted = true` como valor, nunca borra una fila.

`append` es la vía de los conjuntos `append_only`, como el registro de repasos: no pasa por la fusión y no admite `set` ni `del`. El núcleo lo rechaza según el `append_only` del manifiesto, no según la buena fe de la app.

**Cuotas, y las decide el núcleo.** El manifiesto no las declara, así que la app no puede pedir más: hay un autor y tres apps, y una app que negocia su cuota es complejidad de tienda de apps.

| Conjunto | Tope | Por qué |
| --- | --- | --- |
| Entidades fusionables, por app | 1 MB y 4096 entidades | Una lista de la compra con 4096 ítems ya es un fallo de otra clase |
| `append_only`, por app | 8 MB | El registro de repasos crece unos 600 KB al año (ver su documento), así que son más de diez años |
| Nodos por árbol de vista | 256 | Una pantalla de 800×480 paginada no llega a tanto, y acota la memoria de `render` |
| Valor de un campo | 4 KB | `text` paginado; lo que no quepa es contenido, y el contenido va por C1 |

Al llegar al tope, `N0_ERR_QUOTA`, y la app tiene que mostrarlo como un `error` con qué hacer. **Lo que no se hace es escribir a medias ni descartar en silencio.**

---

## 9. Errores

Clasificados, con **frase humana y código corto**, y con lo que se puede hacer. Nada de «Error 7».

* Forma del código: `<ÁREA>-<nn>`, con áreas cerradas: `APP`, `STORE`, `SYNC`, `NET`, `SD`, `PWR`, `TIME`, `UPD`, `SYS`.
* **La frase vive en el inventario de textos**, no en el código, y **no se construye por concatenación**: un mensaje montado a trozos no se puede revisar ni traducir, y la sección 13 exige que el inventario se revise entero.
* Todo error que el usuario puede ver distingue lo que no se arregla solo —un `401` que necesita reaprovisionar— de lo que sí —«no hay WiFi ahora mismo»—, porque son dos frases y dos acciones distintas.
* Sin conexión **no es un error**. Es el modo normal. Lo que cambia es la frescura del dato, y eso se dice una vez y bien, no con un aviso en cada pantalla.

---

## 10. La pila de pantallas

**El kit de interfaz en C es el dueño de la pila**, y el núcleo tiene que poder **desenrollarla unilateralmente**: batería baja, matar una app que se porta mal, volver a dormir. Por eso el kit va en C y no en una app: si la pila viviera dentro de una app, una app atascada bloquearía al árbitro y el sistema no podría garantizar su único invariante.

Desenrollar no pregunta. Llama a `on_stop` con su presupuesto de 20 ms y, si no vuelve, abandona igual: cierra los descriptores, tira la arena y libera los cerrojos. **El invariante de dormir siempre gana.**

---

## 11. Versionado de la API

`NITR0S_API_VERSION` y `NITR0S_API_FLOOR`, los dos enteros. Una app se carga si y solo si `FLOOR ≤ api_required ≤ VERSION`.

* **Aditivo dentro de una versión:** añadir un servicio al final de `n0_services_t` o un nodo nuevo al conjunto no sube `VERSION`… **y esto es una trampa que conviene nombrar**: añadir un campo a la mitad de una estructura, reordenar `n0_services_t` o cambiar el significado de un enumerado **sí** rompe la ABI aunque el código compile. Regla: los miembros se añaden **al final**, los enumerados **nunca se renumeran** y los campos **nunca cambian de significado**, igual que los `field_id` de 4a §6.2.
* Subir `FLOOR` es dejar de cargar apps viejas, y es una decisión consciente que se documenta en el registro de decisiones.
* Las tres apps de v1 van compiladas en el firmware, así que hoy la versión es una formalidad. Deja de serlo **el día que haya la primera app no compilada dentro**, que es exactamente lo que el hito aplazado de Lua necesita de este documento y la razón de escribirlo ahora y no entonces.

---

## 12. Lo que queda por decidir, y lo que esto desbloquea

Abierto, y nada de ello toca la cabecera:

* Los **campos exactos de cada nodo** más allá de los enumerados aquí: se afinan al construir la primera pantalla en el simulador, que es donde se ve si falta uno.
* El **mapeo físico** de acciones semánticas a botones: depende de Q7 y es del módulo de entrada, no de la ABI.
* Los **presupuestos reales** de cada gancho: los de §2 son objetivos y se ajustan con medidas.
* Toda la **fase visual**.

**Y lo que queda desbloqueado:** el paso 5, el simulador. Con 4a, 4b, 4c y el registro de repasos escritos, lo que falta para la Puerta 1 —las tres apps de v1 de extremo a extremo contra el servidor real, la matriz de conflictos en verde y la máquina de estados del árbitro probada— ya no espera ninguna decisión ni ningún hardware.
