# Registro de operaciones y sincronización — especificación de formato y protocolo

* **Autor:** aitorTD
* **Fecha:** 2026-11-27
* **Estado:** especificación de diseño, no implementación. Nada de aquí se ha ejecutado ni medido.
* **Origen:** paso 4b de la cola de arranque de `concepts_brief.md`.
* **Alcance:** fija el modelo de datos, el formato de los registros, el algoritmo de fusión, el sobre y el protocolo. Se absorberá en D4 cuando exista.
* **Hermano:** `registro-de-repasos-y-planificador.md` cubre la tercera clase de sincronización para la app de tests. Este documento cubre las tres clases y es el que manda en el sobre y en la autenticación.

---

## 0. El principio que ordena todo el documento

Decisión 11 del brief: **se congela el formato antes de escribir el servidor, de modo que los mismos bytes viajen por HTTPS, en la tarjeta o por `rsync`.**

Eso tiene una consecuencia que conviene decir antes de nada: **no hay un «formato de fichero» y un «formato de red» que haya que mantener de acuerdo. Hay uno.** Un lote de operaciones es un fichero. Enviarlo es `POST` del fichero; copiarlo a mano es copiar el fichero; respaldarlo es copiar el fichero. Todo lo que sigue describe **un** formato, y el transporte es un detalle que no lo toca.

---

## 1. Las tres clases, y por qué no son una

El brief ya lo fijó: diseñar una sincronización genérica para las tres falla en las tres direcciones. Cada clase tiene su extremo, su formato y su semántica de fallo.

| Clase | Qué mueve | Dirección | Semántica |
| --- | --- | --- | --- |
| **C1 — contenido** | paquetes de apps, bancos de preguntas | Pi → aparato | Versionado, direccionado por hash, **se reemplaza entero**, sin fusión |
| **C2 — ítems** | listas, ítems, ajustes compartidos | bidireccional | Fusión del registro de operaciones, por campo (§4) |
| **C3 — eventos** | repasos, diagnóstico, contabilidad de energía | aparato → Pi | **Solo añadir**, con clave de idempotencia, sin fusión |

Este documento especifica C2 en detalle, y el sobre, la autenticación y la política de disparadores que comparten las tres.

---

## 2. Identidad de las entidades

### 2.1. El identificador, y por qué no es un UUIDv4

Los dos lados crean ítems sin conexión, así que el identificador se acuña en el creador y tiene que ser único sin coordinación. El brief apuntaba a UUIDv4 o ULID con espacio de nombres por creador, y señalaba la trampa: **`esp_random()` solo es un generador de hardware de verdad cuando la radio está activa**, que es precisamente el caso que no se da cuando creas un ítem sin conexión.

**Decisión: el identificador es un par, no un número al azar.**

```
entity_id = (creator_id u64, creator_seq u64)      // 16 bytes
```

`creator_id` es el `device_id` del aparato, o el identificador del servidor, o el del asistente. `creator_seq` es un contador monótono por creador que **no se reinicia nunca** y se persiste **antes** de usarse.

Por qué es mejor que un UUIDv4 aquí: no necesita entropía, así que la debilidad de `esp_random()` sin radio deja de importar; no necesita reloj, así que la parte temporal de un ULID no se llena de basura cuando el BM8563 marca 1970; es único por construcción y no por probabilidad; ordena de forma determinista; y ocupa lo mismo. La objeción del brief a los enteros monótonos era que un autoincremento **global** no convive con la creación sin conexión, y es correcta: lo que no convive es el contador compartido, no el contador por creador.

### 2.2. Los campos son números, no cadenas

Una operación se refiere a un campo por su `field_id` numérico, asignado en el esquema que publica el manifiesto de la app. Es más barato en la tarjeta y en la radio, pero el motivo real es otro: **obliga a que el esquema sea la autoridad**. Si las operaciones llevaran nombres de campo, dos lados podrían discrepar sobre qué significa un nombre sin que nada lo detecte.

---

## 3. La operación

### 3.1. Campos

| Campo | Tipo | Por qué está |
| --- | --- | --- |
| `schema` | `u8` | Versión del esquema del registro. Solo hacia delante y solo aditiva. |
| `op` | `u8` | `1` create, `2` set_field, `3` delete. |
| `author_id` | `u64` | Quién la emitió. |
| `author_kind` | `u8` | `1` aparato, `2` consola (humano), `3` asistente. Es el `origen` del brief, y es lo que permite que la consola diga quién sobrescribió a quién. |
| `lamport` | `u64` | **El primitivo de orden.** Monótono por autor, persistido antes de usarse. |
| `wall_clock` | `i64` | Segundos Unix. **Solo para mostrar. Nunca ordena nada.** |
| `entity_type` | `u16` | Qué clase de entidad (§6). |
| `entity_id` | `16 B` | `(creator_id, creator_seq)` de §2.1. |
| `field_id` | `u16` | Qué campo. `0` reservado para `deleted`. Ignorado en `create`. |
| `field_version` | `u64` | **La versión lógica de ese campo concreto.** Es lo que hace que la fusión sea por campo y no por ítem. |
| `value_len` | `u16` | Longitud del valor. |
| `value` | variable | Tipado por el esquema. |

### 3.2. Marco y almacenamiento

El mismo marco que el resto del aparato, de la sección B3 del brief:

```
+--------+---------------------------+--------+
| len u32| carga (los campos §3.1)   | crc32  |
+--------+---------------------------+--------+
         ^-- crc32 cubre len y carga ---------^
```

* `/nitr0s/sync/out.log` — **el registro de salida propio**, solo se añade.
* Al arrancar, pasada de recuperación: validar `len` y `crc32` y **truncar hasta el último registro válido**.
* Cualquier escritura que no sea un añadido va por fichero temporal en el mismo directorio → `fsync` → renombrado.
* **Tarjeta extraída:** modo degradado de solo lectura, avisar, abortar la sincronización y **volver a deep sleep**.
* **Otra tarjeta insertada:** el marcador con `device_id` y UUID de la tarjeta no coincide → **no se fusiona nada** y se ofrece adopción explícita.

**Cada lado es dueño de su propio registro de salida.** No hay un registro compartido: eso es lo que el singular del concepto original escondía, y un diseño de registro compartido no puede funcionar sin red.

### 3.3. Compactación

Al contrario que el registro de repasos, este **sí** se compacta, porque aquí sí hay registros muertos: diez ediciones del mismo campo dejan nueve inútiles.

Compactar es escribir un fichero nuevo que contenga, por entidad, el conjunto mínimo que reproduce el estado fusionado actual —el `create`, el último `set_field` de cada campo y el `deleted` si lo hay— y renombrarlo encima. Con una condición que es fácil de romper y rompe la fusión: **la compactación conserva los `field_version` originales, no los renumera.** Un registro compactado tiene que seguir siendo fusionable contra el del otro lado, y renumerar lo convertiría en una mentira sobre el orden.

No se compacta por debajo de la marca de agua confirmada por el par: hasta que el otro lado ha confirmado, esas operaciones todavía hacen falta tal cual.

### 3.4. Las marcas de agua van en NVS, no en la tarjeta

Dos por par: `acked_through`, el `lamport` contiguo más alto del registro propio que el par confirmó, y `received_through`, el más alto recibido de él.

Viven en **NVS**, con la hora de la última sincronización, y no en la tarjeta. El motivo es la restricción sutil que el brief ya había identificado: la decisión de **sincronizar al cogerlo** se toma en el arranque temprano, por energía, **antes de montar la SD**. Si la marca de agua viviera en la tarjeta, decidir si merece la pena encender la radio exigiría montar un sistema de ficheros FAT primero.

---

## 4. La fusión, como algoritmo

### 4.1. Estado de una entidad

```
{ entity_id, campos: { field_id → (valor, field_version, author_kind) },
  deleted: (bool, field_version, author_kind) }
```

`deleted` es el campo `0`. **Un borrado es un valor, no una ausencia**, que es lo que permite compararlo.

### 4.2. La regla

Por cada campo, por separado:

1. Gana el `field_version` **mayor**.
2. Si empatan, gana el lado **dueño de la entidad** según su `owner` del esquema (§6). Para `shared`, gana el aparato: es ahí donde «gana el dispositivo» vive de verdad, reducido a lo que siempre debió ser, **un desempate cuando el tiempo lógico es igual**, y no una política general.
3. Si todavía empatan —mismo versión, mismo dueño, dos autores distintos—, gana el `author_id` menor. Es arbitrario, pero es **determinista en los dos lados**, que es lo único que hace falta.

**Las creaciones nunca son conflicto.** Un `create` de un `entity_id` que el otro lado no ha visto nunca se fusiona siempre, sin comparar nada. Esta única línea es la que salva las escrituras del asistente.

**Nunca se descarta en silencio una escritura perdedora.** El perdedor va al registro de conflictos del servidor con su autor y su motivo, y la consola muestra «esta edición fue sobrescrita por el aparato». Sin eso, la sincronización parece simplemente rota.

### 4.3. Ejemplos trabajados

**E1 — El caso que el concepto tenía mal.** La consola borra «leche» (`deleted=true`, versión 5). Después el aparato marca «leche» como hecha (`done=true`, versión 6). Fusión por campo: `done` toma la versión 6, `deleted` **sigue en true porque nadie lo tocó**.

Estado final: **el ítem sigue borrado**, y la edición del aparato se conserva sin resucitarlo. Esto **cambia el comportamiento que el concepto original describía**, donde el ítem volvía. Es deliberado: ahí estaba la lista encantada. Resucitar algo exige escribir `deleted=false` explícitamente, que es una acción con nombre y no un efecto secundario de tocar otro campo.

**E2 — Borrado en los dos lados.** Convergen al mismo valor. No hay conflicto que registrar.

**E3 — Edición contra edición del mismo campo.** La consola pone `text="leche entera"` (versión 7); el aparato, sin conexión, pone `text="leche"` (versión 7). Empate, entidad `shared` → **gana el aparato**. La consola muestra que su edición fue sobrescrita, con el texto perdido visible.

**E4 — El asistente escribe.** El asistente crea un ítem con su propio `creator_id`. Es un `create` que nadie ha visto: se fusiona siempre y **no puede perder**. Esto es lo que el brief exigía al detectar que «gana el dispositivo» mataba precisamente las escrituras del asistente.

**E5 — El límite honesto de este protocolo.** El asistente vuelve a añadir «leche» después de que el aparato la borrara. Como es un `create` con un `entity_id` nuevo, **aparece un ítem nuevo**, y el protocolo no puede ni debe impedirlo: desde su punto de vista son dos ítems distintos. La espiral de la lista encantada, en esta forma, **no es un problema de replicación y no se arregla aquí**: es la app de Listas la que tiene que deduplicar por nombre normalizado contra los ítems activos en el momento de crear, y avisar en vez de duplicar. Queda escrito como requisito de la app, no del protocolo, para que nadie lo busque más tarde en el sitio equivocado.

**E6 — Reenvío de un lote duplicado.** Deduplicado por `op_id = (author_id, lamport)`. Idempotente, sin efecto.

### 4.4. Propuestas del asistente: no son operaciones

De la corrección del brief: las peticiones explícitas del autor producen cambios efectivos, y solo lo que el asistente origina por iniciativa propia entra como **propuesta**.

**Una propuesta no entra nunca en el registro de operaciones.** Vive en una tabla aparte del servidor hasta que el autor la aprueba, y al aprobarla se emite la operación correspondiente, con `author_kind=3` y la marca de que fue aprobada. El registro contiene **hechos**; si contuviera propuestas, todo lector tendría que saber distinguirlas y acabaría por no saberlo.

---

## 5. El sobre y el protocolo

### 5.1. Cabecera del lote

`magic "nitr0Sop"`, `proto_version u16`, `dataset_id u64`, `sender_id u64`, `sender_kind u8`, `envelope_seq u64`, `op_count u32`, `acked_through u64`, `hmac 32 B`.

**Aclaración que salió al implementarlo:** el `acked_through` del sobre es la marca de agua **del flujo del receptor** que el remitente dice tener ya, no la del suyo. Lo contrario no serviría de nada, porque nadie necesita que le cuenten su propio registro. Los dos valores de §3.4 siguen guardándose en NVS; el que **viaja** es este.

**Y una segunda:** la marca de agua es **un escalar por par**, así que todas las operaciones del lado del servidor comparten **una sola secuencia de `lamport` bajo el mismo `author_id`**, y lo que distingue a la consola del asistente es `author_kind`. Eso se desprende de la tabla de principales del brief: aparato, humano y agente son **principales de autenticación, no pares de replicación**. Si el asistente fuera un par aparte, un escalar no podría describir su estado. El `creator_id` de lo que crea el servidor es también el del servidor, y el `creator_seq` lo reparte un solo asignador, así que la unicidad de §2.1 se mantiene.

El `dataset_id` está para que **un cambio destructivo de esquema no pueda fusionarse en silencio**: la política es solo hacia delante y solo aditiva, y un cambio destructivo exige un `dataset_id` nuevo, con lo que los lotes viejos dejan de encajar en vez de corromper.

### 5.2. Autenticación, y un mecanismo que hace dos trabajos

**El HMAC es el autenticador y el TLS es confidencialidad y comodidad.** No es un apaño: es lo que hace que un saludo inicial con validez relajada sea un diseño deliberado, y es necesario porque el TLS termina en Funnel y no en la Pi.

El HMAC cubre el sobre entero con el secreto del aparato. La rotación de claves se hace con el servidor aceptando **dos secretos válidos durante una ventana de solape**.

Y el antirreplay: **`envelope_seq` es un contador monótono por aparato que el servidor recuerda, y rechaza todo lo que no sea estrictamente mayor que el último visto.** Dos cosas que esto resuelve de golpe, y por las que se elige frente a un nonce:

* Es **correcto con un reloj arbitrariamente equivocado**, que es la situación normal de este aparato tras quedarse sin batería.
* **Elimina la caché de nonces**, y con ella el riesgo que el brief había señalado: una caché sin límite es una fuga lenta de memoria en una Pi de 1 GB, y una caché ausente hace que el nonce no sirva de nada. Un contador no tiene tamaño.

Un reintento usa un `envelope_seq` nuevo con las mismas operaciones dentro: el antirreplay lo acepta y la idempotencia por `op_id` impide que haga nada dos veces. Las dos propiedades conviven porque viven en campos distintos.

**Desvío de reloj:** el cuerpo del `401` lleva la hora del servidor, el aparato ajusta su reloj y **reintenta exactamente una vez**. Sin esto, un aparato con el reloj desviado se queda bloqueado para siempre con un mensaje inútil y sin teclado donde arreglarlo.

**Implementado así, con dos precisiones:** *todos* los 401 llevan la hora, y un aparato desconocido y una firma mala dan el **mismo** 401, para no filtrar si el aparato existe. Y `/hello` va **sin autenticar** a propósito: es lo primero que necesita un aparato que ha perdido el reloj, y no entrega nada secreto, porque el autenticador es el HMAC.

### 5.3. Versiones y el contrato congelado

El servidor soporta `proto_version` N y N-1. Ante una no soportada, el aparato **avisa y sigue funcionando sin red**, que es su modo normal y no una degradación.

**Invariante en mayúsculas, del brief: EL APARATO SOLO SE PUEDE ACTUALIZAR A TRAVÉS DEL MISMO SERVIDOR.** Por tanto el extremo de saludo y actualización es un **contrato congelado y nunca versionado**. Un cambio de protocolo que rompa el camino de actualización es irrecuperable sin USB, y este aparato puede no tener USB.

### 5.4. Disparadores y presupuesto de reintentos

El presupuesto pertenece a este documento y su aplicación al árbitro de sueño; los dos tienen que ponerse de acuerdo explícitamente.

* Al **quinto fallo consecutivo**, espera creciente, no otro reintento.
* **Cuarentena: dos fallos consecutivos** de la misma app, con reactivación manual. Es el umbral único de todo el proyecto.
* **El fallo del gancho de sincronización de una app no aborta el de las demás.**
* No sincronizar por debajo del suelo de batería, reservando lo justo para pintar una pantalla y dormir; y **serializar las escrituras en la tarjeta contra las ráfagas de radio**, porque el peor evento de corriente es una escritura en SD a la vez que una transmisión WiFi al 10%.
* Objetivo de vivacidad del brief: **≥95% de las sincronizaciones acaban en 8 s de radio encendida.**

---

## 6. Entidades de v1 y su propiedad

`owner` se declara en el esquema, no en el código.

| Entidad | `owner` | Clase | Nota |
| --- | --- | --- | --- |
| Lista (nombre, orden) | `shared` | C2 | «Existe y se llama así» quiere autoridad de la consola, que es la que tiene teclado |
| Ítem de lista | `shared` | C2 | «Hecho / no hecho» quiere autoridad del aparato |
| Catálogo de ítems frecuentes | `device` | — | **Derivado** del historial local, no se sincroniza. Frecuencia y recencia son del aparato |
| Banco de preguntas | `server` | C1 | Se reemplaza entero, direccionado por hash |
| Repaso / resultado de test | `device` | C3 | Solo añadir, nunca entra en la fusión |
| Diagnóstico y contabilidad de energía | `device` | C3 | Solo añadir |
| Ajustes del aparato | `device` | C2 | |
| Conjunto de apps instaladas | — | — | **Dos cosas distintas:** lo marcado en la Pi es el estado *deseado*, el manifiesto de la tarjeta es el *real*, y la **reconciliación** es una operación con nombre |

---

## 7. La matriz de conflictos, como prueba ejecutable

Es el criterio de aceptación «corrección de la sincronización» del brief, que se puede ejecutar **hoy, sin hardware y sin aparato**, contra el servidor real y un aparato falso. Cada caso con su estado final esperado.

| # | Caso | Estado final esperado |
| --- | --- | --- |
| 1 | Edición en aparato contra edición en consola, mismo campo | Gana el aparato; la edición de la consola en el registro de conflictos y visible |
| 2 | Edición en aparato contra borrado en consola, campos distintos | **Sigue borrado**, con la edición conservada (E1) |
| 3 | Borrado en los dos lados | Borrado, sin conflicto registrado |
| 4 | Resucitar: `deleted=false` explícito con versión mayor | Vuelve a estar activo |
| 5 | Aparato sin red 7 días, luego sincroniza | Convergencia en una ronda, cero pérdidas |
| 6 | Tarjeta sacada a mitad de sincronización | Prefijo válido, cero operaciones parciales, modo degradado y vuelta a dormir |
| 7 | Corte de corriente en tres puntos de inyección | Pasada de recuperación deja un prefijo válido en los tres |
| 8 | Desvío de reloj de ±24 h | Orden intacto (manda `lamport`); `401` con hora del servidor y **un** reintento |
| 9 | Reenvío de un lote duplicado | Sin efecto, por `op_id` |
| 10 | Tarjeta ajena insertada | No se fusiona nada; se ofrece adopción explícita |
| 11 | Ítem escrito por el asistente | Sobrevive siempre, o aparece en el registro de conflictos (E4) |
| 12 | Propuesta del asistente no aprobada | **No aparece** en el registro de operaciones ni en el aparato |
| 13 | `dataset_id` distinto | Lote rechazado, sin fusionar nada |
| 14 | `proto_version` no soportada | Aviso y funcionamiento sin red; nada corrupto |
| 15 | Compactación a mitad de camino | El registro compactado fusiona igual que el original (§3.3) |

**Aprobado = cero ítems perdidos, cero duplicados, convergencia en una ronda.** Los casos 12, 13, 14 y 15 son nuevos respecto a la lista del brief, y salen de escribir el formato: son precisamente los que un diseño sin especificar deja sin probar.

---

## 8. Lo que este documento no decide

* El esquema concreto de cada entidad campo a campo: va con el manifiesto, paso 4a.
* La API del núcleo que las apps usan para leer y escribir: paso 4c.
* La topología de Funnel y **dónde termina de verdad el TLS**: va en D5, y es la razón por la que el HMAC no es opcional.
* El formato de los paquetes de apps y su distribución, que es C1 y tiene su propio flujo.
