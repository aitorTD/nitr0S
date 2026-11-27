# Registro de repasos y planificador — especificación de formato e interfaz

* **Autor:** aitorTD
* **Fecha:** 2026-11-27
* **Estado:** especificación de diseño, no implementación. Nada de aquí se ha ejecutado ni medido.
* **Origen:** paso 4 de la cola de arranque de `concepts_brief.md`, desbloqueado al resolver T1 y T2 el 27-11-2026.
* **Alcance:** fija el formato del registro de repasos, la forma del estado derivado y la interfaz del planificador. Se absorberá en D3 (especificación de apps) y D4 (protocolo de replicación) cuando esos documentos existan.

---

## 0. Qué fija este documento y qué deja abierto a propósito

**Fija:** la identidad de las preguntas, los campos exactos del evento de repaso, su codificación en la tarjeta, el comportamiento con el reloj no fiable, la forma del estado derivado, la interfaz del planificador, la aritmética con la que calcula y la prueba que respalda que sea reemplazable.

**Deja abierto, y no por descuido:** el algoritmo (T0), la política de reinicio al editar (T3), los topes diarios (T4), las estadísticas (T5) y el detalle de la puntuación (T6). Ninguno de los cinco toca el formato, y ese es justamente el criterio con el que está diseñado: **si una decisión pendiente obligase a cambiar el formato, no estaría pendiente, estaría mal planteada.**

Lo que este documento **no** es: no es el esquema del banco de preguntas (eso es contenido, propiedad del servidor, y va en D3), ni la interacción de la app, ni el diseño de pantallas, que la sección 12 del brief deja explícitamente para la fase visual.

---

## 1. Identidad

Cuatro identificadores, y las reglas que los hacen utilizables.

| Identificador | Quién lo acuña | Tipo | Regla |
| --- | --- | --- | --- |
| `question_id` | la consola, al crear la pregunta | `u64` | Estable para siempre y **nunca reutilizado**, ni tras borrar la pregunta. No se deriva del contenido ni de la posición. El validador rechaza publicar un banco que reutilice uno o que repita dos. |
| `bank_id` | la consola | `u32` | Estable. Mover una pregunta de banco **no** cambia su `question_id` ni su progreso. |
| `device_id` | el aparato, en el aprovisionamiento | `u64` | Derivado de la MAC de fábrica. Identifica al productor de eventos. |
| `session_id` | el aparato | `u64` | Contador local monótono. Único globalmente al ir junto a `device_id`. Es lo que permite agregar una sesión y dar su nota. |

**`op_id` es derivado, no sorteado:** `op_id = (device_id, lamport_seq)`, 16 bytes. Es a la vez el identificador del evento y **la clave de idempotencia de la subida**, así que no hay dos conceptos que mantener de acuerdo. `lamport_seq` es un `u64` monótono por aparato que no se reinicia nunca, ni al perder el reloj, ni al reiniciar, ni al reaprovisionar; vive en NVS y se persiste **antes** de escribir el evento que lo usa.

---

## 2. El evento de repaso

Un evento por respuesta. Inmutable, solo se añade, nunca se edita ni se borra.

### 2.1. Campos

| Campo | Tipo | Por qué está |
| --- | --- | --- |
| `schema` | `u8` | Versión del esquema del registro. Política del brief: solo hacia delante y solo aditiva. |
| `kind` | `u8` | `0` repaso, `1` ancla de reloj (§4), `2` reinicio de progreso (§7). Hace el formato extensible sin romper a los lectores viejos. |
| `device_id` | `u64` | Productor. Con `lamport_seq` forma el `op_id`. |
| `lamport_seq` | `u64` | **El primitivo de orden.** Ordena el historial sin depender del reloj. |
| `question_id` | `u64` | A qué pregunta se refiere. |
| `bank_id` | `u32` | Para estadísticas por banco sin mirar el contenido. |
| `session_id` | `u64` | Para agregar la sesión y dar su nota. |
| `session_mode` | `u8` | `0` examen, `1` repaso. **Decisión T2.** Sin este campo los dos modos contaminan el mismo historial. |
| `grade` | `u8` | `1` otra vez, `2` difícil, `3` bien, `4` fácil. **Decisión T1: cuatro niveles siempre, aunque v1 solo ofrezca dos.** |
| `grade_source` | `u8` | `0` derivada del acierto, `1` puesta por el autor. **Decisión T1 y T2.** Permite que un planificador futuro pondere distinto una valoración inferida, o la descarte, sin tocar el pasado. |
| `prev_state` | `u8` | `0` nueva, `1` aprendiendo, `2` repaso, `3` reaprendiendo. Lo que FSRS necesitaría y no se puede reconstruir si no se guarda. |
| `correct` | `u8` | `0` o `1`. En modo examen es la señal primaria; en modo repaso va junto a la valoración. |
| `answer_given` | `u8` | Índice de la opción elegida, `0xFF` en blanco. Permite análisis de distractores sin volver a preguntar. |
| `clock_trusted` | `u8` | `0` o `1`. **El campo que evita mentir** (§4). |
| `reviewed_at` | `i64` | Segundos Unix del reloj de pared. **Solo para mostrar y para calcular intervalos cuando es de fiar; nunca para ordenar.** |
| `mono_ms` | `u64` | Milisegundos desde una época persistida en NVS, monótona entre reinicios. Es lo que ordena de verdad cuando el reloj no vale. |
| `elapsed_days` | `u16` | Días desde el repaso anterior de **esta** pregunta. `0xFFFF` = desconocido o primer repaso. |
| `prev_interval_days` | `u16` | Intervalo vigente antes de este repaso. |
| `prev_ease` | `u16` | Factor de facilidad previo **en milésimas** (2500 = 2,5). Entero a propósito: ver §6.3. |
| `latency_ms` | `u16` | Lo que se tardó en responder, saturado a 65 s. Barato de guardar e imposible de reconstruir después. |
| `scheduler_version` | `u16` | Qué planificador produjo el estado previo. Permite detectar y auditar un recálculo. |
| `progress_epoch` | `u32` | La época de progreso vigente de la pregunta (§7). Lo que hace que un reinicio sea auditable en vez de una pérdida. |

### 2.2. Codificación en la tarjeta

Empaquetado explícito, **little-endian, sin relleno**, con una comprobación estática del tamaño en la compilación. El marco es el de la sección B3 del brief, igual que el resto de los registros del aparato:

```
+--------+-------------------------+--------+
| len    | carga (los campos §2.1) | crc32  |
| u32    | len bytes               | u32    |
+--------+-------------------------+--------+
         ^-- crc32 cubre len y carga --------^
```

La carga son unos **78 bytes**, así que el marco añade un 10%. Una sesión de 20 preguntas ocupa 1,7 KB y un año de veinte repasos diarios ocupa unos **600 KB**. Esto es lo que hace que la regla «los eventos de repaso no se borran nunca» no tenga coste: no hay presión de espacio que la discuta.

**Y una consecuencia que simplifica las cosas respecto al registro de operaciones general:** el registro de repasos **no se compacta nunca**. La compactación existe para recuperar espacio de registros muertos, y aquí no hay registros muertos porque nada se edita ni se borra. Solo la caché derivada se reescribe, y se reescribe entera (§5).

* Fichero: `/nitr0s/apps/tests/reviews.log`, abierto en modo añadir.
* Al arrancar, **pasada de recuperación**: recorrer validando `len` y `crc32`, y **truncar hasta el último registro válido**. Un corte de corriente o una extracción de la tarjeta a mitad de escritura deja un registro roto al final, que es exactamente el caso que esto resuelve.
* **Tarjeta extraída:** modo degradado de solo lectura, se avisa, se aborta la sincronización y **se vuelve a deep sleep**. No se falla.
* **Otra tarjeta insertada:** el marcador con `device_id` y UUID de la tarjeta no coincide, así que **no se fusiona nada** y se ofrece una adopción explícita. Un registro de repasos ajeno fusionado en silencio sería indetectable después.
* `fsync` después de cada lote de una sesión, no después de cada evento: una sesión es la unidad que el usuario percibe, y veinte `fsync` seguidos en FAT no compran nada.

---

## 3. Orden

El historial de una pregunta se ordena por `lamport_seq`. Como el aparato es el **único productor** de eventos de repaso, ese orden es total y no hay empates que deshacer. Si alguna vez hubiera un segundo aparato, el orden pasa a ser `(lamport_seq, device_id)`, que es determinista y está escrito aquí para no inventarlo entonces.

`reviewed_at` **no ordena nada.** Esa es la regla del brief sobre el reloj de pared y es la que evita que un BM8563 que marca 1970 reordene el historial.

---

## 4. Reloj no fiable

El problema de esta app no es la red, es el reloj.

* Cuando `clock_trusted = 0`, el evento se registra igual, con su `mono_ms`, y con `elapsed_days = 0xFFFF`.
* **El planificador no inventa fechas.** Con `elapsed_days` desconocido, cuenta el repaso —avanza la cuenta de repeticiones y aplica el efecto de la valoración sobre la facilidad— pero **no hace crecer el intervalo**, y marca el estado derivado como `provisional`. La programación degrada a **mantener el orden relativo**, que es lo que el brief decidió.
* La app lo dice en pantalla. No simula normalidad.
* Cuando vuelve una hora de confianza se escribe un **evento `kind = 1`, ancla de reloj**, con el par `(mono_ms, reviewed_at)`. Eso permite reconstruir *a posteriori* una fecha aproximada para los eventos marcados, interpolando entre anclas, **sin reescribir ni un byte del pasado**. Es la diferencia entre anotar la historia y falsificarla.
* Las anclas son eventos del mismo registro, así que suben por el mismo camino y la Pi puede hacer la misma interpolación con el mismo código.

---

## 5. Estado derivado

Derivado, recalculable y **nunca autoritativo**. Por pregunta: próxima fecha, intervalo en días, facilidad en milésimas, repeticiones, recaídas, última valoración, estado (`nueva` / `aprendiendo` / `repaso` / `reaprendiendo`), bandera `provisional` y `progress_epoch` vigente.

Se guarda como caché en `/nitr0s/apps/tests/sched.cache` para no recorrer el historial en cada arranque, y se escribe **entero** por fichero temporal en el mismo directorio → `fsync` → renombrado. Nunca se muta en el sitio.

La caché lleva una cabecera con `event_count`, el `crc32` del **último** registro del log y `scheduler_version`. Comprobar si está obsoleta es entonces **O(1)**: si cualquiera de los tres no cuadra con el log, se descarta la caché y se reconstruye recorriendo. Y si se corrompe, se descarta: no hay nada que recuperar porque no contiene ningún hecho.

Las estadísticas y la nota de cada sesión son **también derivadas**, agregando por `session_id`. No se guarda ninguna nota como hecho; se recalcula. Eso es lo que impide que la nota y el historial puedan discrepar.

---

## 6. La interfaz del planificador

### 6.1. Forma

Un pliegue sobre el historial de **una** pregunta. Entra el historial, sale la próxima fecha. Nada más conoce las interioridades.

```c
// nitr0s_sched.h — la costura estrecha. Ningún otro módulo incluye nada más del planificador.
#include <stdint.h>

typedef struct {               // un evento ya decodificado, §2.1
  uint8_t  kind, session_mode, grade, grade_source, prev_state, correct, clock_trusted;
  uint16_t elapsed_days, prev_interval_days, prev_ease, latency_ms;
  int64_t  reviewed_at;
  uint64_t mono_ms, lamport_seq;
  uint32_t progress_epoch;
} nitr0s_review_t;

typedef struct {               // el estado derivado de §5
  int64_t  due;                // segundos Unix; INT64_MIN si provisional sin fecha
  uint16_t interval_days, ease_milli, reps, lapses;
  uint8_t  state, last_grade, provisional;
  uint32_t progress_epoch;
} nitr0s_sched_state_t;

void     nitr0s_sched_reset(nitr0s_sched_state_t *st);
void     nitr0s_sched_apply(nitr0s_sched_state_t *st, const nitr0s_review_t *ev);
uint16_t nitr0s_sched_version(void);
```

### 6.2. Lo que la interfaz prohíbe

Las tres reglas que hacen que el aplazamiento de T0 sea real y no un agujero:

* **`nitr0s_sched_apply` es pura.** Sin entrada ni salida, sin asignación de memoria, sin globales, **sin leer el reloj** y sin registro. Todo lo que necesita viene en `st` y en `ev`. Un planificador que lee el reloj no es reproducible, y entonces el recálculo no vale nada.
* **Quién lee el reloj:** el armado de la sesión, no el planificador. El planificador produce una fecha; decidir que hoy toca es de quien tiene el reloj delante.
* **Nadie más conoce sus constantes.** Ni la interfaz, ni la sincronización, ni la consola. Si un segundo sitio conoce el factor de facilidad inicial, el algoritmo deja de ser reemplazable.

### 6.3. Aritmética: enteros, y por un motivo concreto

**Solo aritmética entera, nada de coma flotante.** El factor de facilidad va en milésimas (`u16`), los intervalos en días enteros, y el redondeo está especificado: **media hacia arriba**.

El motivo no es el rendimiento del ESP32-S3, es la **reproducibilidad entre dos implementaciones distintas**: el aparato calcula en C y el servidor en Go, y el criterio de aceptación del brief exige que los dos produzcan **las mismas fechas al día** con el mismo registro. Con coma flotante eso depende del orden de las operaciones, del compilador y de la biblioteca matemática de cada lado, y falla de formas que solo aparecen meses después y en pocas preguntas. Con enteros y redondeo especificado, es una igualdad comprobable.

Esto acota T0 sin decidirlo: **el algoritmo que se elija tendrá que poder expresarse en aritmética entera.** SM-2 y las cajas de Leitner lo cumplen de sobra. FSRS no, así que si algún día se adopta, la consecuencia es que **las fechas las calcula un solo lado y el otro las recibe**, y el criterio de igualdad cambia de forma. Mejor escrito ahora que descubierto entonces.

### 6.4. Versionado

`scheduler_version` es un `u16` que sube con cualquier cambio de comportamiento, incluido un cambio de constantes. Lo que corra en v1 es **provisional y está etiquetado como tal**, con su versión visible en los ajustes de la app. Cambiar de algoritmo es un recálculo desde el registro, no una migración: se descarta la caché y se vuelve a plegar.

---

## 7. Editar, retirar y reiniciar progreso sin violar la propiedad del estado

Aquí había un conflicto real entre dos decisiones ya tomadas del brief, y conviene dejar resuelto el mecanismo.

Los bancos son **del servidor** y los repasos son **del aparato**, de un solo sentido y sin fusión. Pero un reinicio de progreso lo decide la consola, que es quien tiene el teclado y quien ve que la respuesta correcta ha cambiado. Si la consola escribiera un evento en el registro de repasos, estaría escribiendo en un flujo que no le pertenece, y la propiedad del estado dejaría de valer.

La salida no cuesta nada y mantiene las dos reglas intactas:

1. Cada pregunta del banco lleva, **en el contenido**, un `content_hash` sobre los campos con significado y un **`progress_epoch`** (`u32`).
2. Cuando la consola decide que una edición invalida lo aprendido —según T3, que sigue abierta— **incrementa `progress_epoch`** de esa pregunta. Eso viaja por el canal de contenido, que es de un solo sentido y propiedad del servidor. Nada más cambia.
3. El aparato, al descargar el banco, ve la época nueva y **escribe él mismo un evento `kind = 2`** con la época nueva y el motivo. El evento es suyo, en su flujo, por su camino.
4. El planificador **ignora los eventos con una época anterior a la vigente** al plegar. El progreso se reinicia sin que se borre un solo hecho.

Resultado: el reinicio es auditable, reproducible idénticamente en los dos lados, reversible —se puede plegar ignorando el corte para ver qué había— y no viola la propiedad de ningún flujo. **Y T3 queda reducida a lo que de verdad es: una decisión de política sobre cuándo la consola incrementa la época, no una decisión de formato.**

Lo demás sigue como el brief lo fijó: retirar una pregunta la saca de la programación pero **no borra sus eventos**; mover de banco conserva el progreso; y los repasos huérfanos de una pregunta ya retirada **suben igual** y la Pi los archiva contra el identificador retirado.

---

## 8. Sincronización

Tercera clase del brief: **eventos que solo se añaden, con clave de idempotencia**, en un solo sentido del aparato a la Pi. No hay conflicto que resolver, así que «gana el dispositivo» no se aplica.

* Se sube por lotes, ordenados por `lamport_seq`.
* La clave de idempotencia es el `op_id`. Un reenvío no duplica: la Pi lo tiene o no lo tiene.
* El aparato guarda `uploaded_through`, el `lamport_seq` contiguo más alto confirmado. Reenviar desde ahí siempre es seguro, así que un corte a mitad de subida no necesita ninguna lógica especial.
* La Pi guarda los eventos **tal cual**, sin interpretarlos al recibirlos, y recalcula el estado derivado con la misma aritmética entera. Guardar el hecho y derivar después es lo que permite que un cambio de planificador no exija volver a pedirle nada al aparato.
* Un lote con un evento cuyo `schema` es mayor que el que la Pi entiende: **se guarda igual** y se marca como no interpretado. La política aditiva garantiza que los campos que conoce siguen en su sitio, y perder un evento sería perder un hecho.

---

## 9. La prueba que respalda aplazar T0

Es el criterio de aceptación «planificador reemplazable» del brief, escrito como algo ejecutable. Se puede escribir **hoy**, sin hardware, y es lo que sostiene la decisión de dejar el algoritmo abierto.

**Prueba A, reemplazo.** Con un historial sintético de varios meses, varias decenas de preguntas y todos los casos raros dentro —eventos con reloj no fiable, anclas de reloj, reinicios de época, repasos huérfanos, los dos modos mezclados, valoraciones derivadas y puestas a mano—: plegar entero con el planificador v1, cambiar a v2 y recalcular, y comparar con plegar entero con v2 desde el primer repaso. **Aprobado = estado idéntico, cero divergencias.** Lo que esta prueba caza de verdad es el estado oculto: si el planificador arrastra algo que no está en el registro, los dos caminos difieren.

**Prueba B, dos implementaciones.** El mismo historial plegado por la implementación en C del aparato y por la de Go del servidor, con la misma `scheduler_version`. **Aprobado = las mismas fechas al día, cero divergencias.** Es lo que la aritmética entera de §6.3 hace alcanzable.

**Prueba C, el marco.** Truncar el fichero de registro en cada byte posible de los últimos registros y comprobar que la pasada de recuperación siempre deja un prefijo válido, que ningún evento parcial se interpreta y que la caché se detecta obsoleta. Es la prueba de la extracción de la tarjeta a mitad de escritura, y cuesta un bucle.

**Si la prueba A o la B no pasan, T0 deja de ser aplazable** y hay que cerrar el algoritmo antes de seguir con la app. Eso ya estaba escrito en el brief; aquí queda dicho con qué se comprueba.

---

## 10. Lo que queda por decidir, y qué desbloquea cada cosa

| id | Decisión | Qué desbloquea | Toca el formato |
| --- | --- | --- | --- |
| T0 | El algoritmo | Nada de v1; corre uno provisional y etiquetado | No |
| T3 | Cuándo la consola incrementa `progress_epoch` | El validador y el flujo de publicación de la consola | No (§7) |
| T4 | Topes diarios de preguntas nuevas y de repasos | El armado de la sesión | No |
| T5 | Qué estadísticas y dónde | Las vistas del aparato y de la consola | No, son derivadas (§5) |
| T6 | Detalle de corrección y puntuación | La pantalla de resultado de la sesión | No, es derivada por `session_id` |
| Q7 | Cuántos botones hay y dónde | **La entrada de cuatro niveles de T1**, que v1 no usa | No |

Las seis están fuera del camino crítico del formato, que es lo que este documento tenía que conseguir.
