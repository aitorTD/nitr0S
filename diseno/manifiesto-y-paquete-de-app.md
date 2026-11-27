# Manifiesto y paquete de app — especificación de formato

* **Autor:** aitorTD
* **Fecha:** 2026-11-27
* **Estado:** especificación de diseño, no implementación. Nada de aquí se ha ejecutado ni medido.
* **Origen:** paso 4a de la cola de arranque de `concepts_brief.md`.
* **Alcance:** disposición del paquete, instalación atómica, manifiesto campo a campo, reglas de `id`, capacidades, declaración de esquemas y reglas de rechazo. Se absorberá en D3 cuando exista.
* **Hermanos:** `registro-de-operaciones-y-sincronizacion.md` (4b, de donde salen `field_id`, `owner` y `dataset_id`) y `registro-de-repasos-y-planificador.md`.

---

## 0. Qué fija y qué no

**Fija:** cómo es un paquete en la tarjeta, cómo se instala sin poder quedarse a medias, qué declara el manifiesto, la lista cerrada de capacidades, el sistema de tipos del esquema y qué hace que un paquete se rechace.

**No fija:** la cabecera de la API del núcleo ni la lista de tipos de nodo de vista, que son el paso 4c; ni el aspecto de nada, que es la fase visual de la sección 12 del brief.

Y una decisión heredada que conviene repetir aquí porque este documento es donde se notaría: **Lua no está en v1.** La costura es `app_t` en C más descriptores JSON. Nada de lo que sigue presupone un intérprete, y el manifiesto no lleva ningún campo que solo tendría sentido con uno.

---

## 1. Disposición en la tarjeta

```
/nitr0s/
    apps/
        index.json                     ← qué versión está viva de cada app. El commit atómico
        tests/
            3/                         ← versión de paquete, monótona por app
                manifest.json
                schema.json            (opcional; si falta, el esquema va dentro del manifiesto)
                icon.1bpp
                views/*.json
                texts/es.json
            4/
                .installing            ← marcador: en cuarentena hasta que se complete
                ...
    data/
        tests/                         ← datos de la app, FUERA del directorio de versión
            reviews.log
            sched.cache
    sync/
        out.log
```

**La separación entre `apps/` y `data/` no es cosmética.** Si los datos vivieran dentro del directorio de versión, actualizar una app borraría su historial. Con esta disposición, el código y los descriptores son reemplazables y versionados, y los datos son persistentes y de la app, no de la versión.

### 1.1. Instalación atómica, sin necesitar que FAT sea atómico

FAT no ofrece un renombrado de directorio que sustituya a otro, así que la atomicidad no se busca ahí. Se busca en un solo fichero:

1. Escribir el paquete nuevo en `apps/<id>/<pkg_version_nueva>/`, con un fichero **`.installing`** dentro desde el primer byte.
2. `fsync` de los ficheros y del directorio.
3. Borrar `.installing`.
4. Reescribir **`apps/index.json`** por fichero temporal en el mismo directorio → `fsync` → renombrado, apuntando la app a la versión nueva.

El paso 4 es el **commit**, y es atómico porque un renombrado de fichero en el mismo directorio lo es. Todo lo anterior es reversible sin consecuencias.

* **El descubrimiento ignora cualquier directorio de versión que tenga `.installing`.** Un corte de corriente a mitad de copia deja basura en cuarentena, no una app corrupta viva.
* **La vuelta atrás es reescribir `index.json`** a la versión anterior, que sigue en la tarjeta. Esto es lo que hace que la versión de paquete monótona gobierne de verdad la instalación, la actualización y el retroceso.
* **Una app no se sustituye mientras está activa:** el cambio de `index.json` surte efecto en el siguiente lanzamiento. La app en marcha sigue leyendo su directorio de versión, que no se ha tocado.
* Se conservan **dos** versiones por app y se borran las anteriores al arrancar, no durante la instalación.

### 1.2. Cómo nota el aparato que algo cambió

Al arrancar y después de cada sincronización: se comparan el hash del manifiesto y de los descriptores con el índice en caché. Si cambian, se recarga en el siguiente lanzamiento.

**Recarga en caliente por vigilancia de ficheros: solo en el simulador**, que es donde iterar importa. En el aparato sería complejidad a cambio de nada.

### 1.3. Reconciliación

El conjunto marcado en la Pi es el estado **deseado**; `index.json` de la tarjeta es el estado **real**. La reconciliación es una **operación con nombre**, no un efecto secundario de sincronizar: compara los dos, descarga lo que falta, retira lo que sobra y deja constancia. La consola muestra las dos columnas, porque «marcada en la Pi» y «instalada en el aparato» son estados distintos y confundirlos es lo que hace que un aparato parezca roto.

---

## 2. El manifiesto

JSON, y solo JSON: cJSON viene con ESP-IDF, y no se mezclan TOML ni YAML en el proyecto.

| Campo | Obligatorio | Tipo | Regla |
| --- | --- | --- | --- |
| `manifest_version` | **sí** | entero | Versión del formato de este fichero. |
| `api_required` | **sí** | entero | Versión de la API del núcleo que la app necesita. §4. |
| `id` | **sí** | cadena | §3. Tiene que coincidir con el nombre del directorio. |
| `pkg_version` | **sí** | entero | Monótono por app. Tiene que coincidir con el nombre del directorio de versión. |
| `name` | **sí** | cadena | Nombre visible, ≤24 caracteres. |
| `capabilities` | **sí** | lista | Lista cerrada de §5. Vacía es válida y es lo normal. |
| `datasets` | no | lista | Conjuntos de datos que la app posee, con su esquema y su `owner`. §6. |
| `icon` | no | cadena | Ruta relativa a un `.1bpp`. Sin icono, el lanzador usa el genérico. |
| `views` | no | lista | Rutas relativas a descriptores de vista. Su contenido es del paso 4c. |
| `texts` | no | objeto | Rutas por idioma. v1 solo necesita `es`. |
| `entry` | no | cadena | Nombre del símbolo `app_t` para apps compiladas en el firmware. Las apps de v1 lo llevan. |
| `min_battery_pct` | no | entero | Suelo por debajo del cual el lanzador no la abre. Para apps que gastan radio. |

**No lleva, a propósito:** autor, descripción larga, categoría, capturas, URL ni licencia. Son campos de tienda de apps, y la tienda está cortada; el brief advertía de que el corte se quedó con la factura, y esto es parte de terminarlo. Hay un solo autor y tres apps.

---

## 3. Reglas de `id`

Un `id` **es un nombre de fichero en FAT**, y eso manda sobre cualquier otra consideración.

* Caracteres: `a`–`z`, `0`–`9` y `-`. Nada más: sin acentos, sin eñes, sin espacios, sin mayúsculas, sin puntos.
* Empieza por letra. Longitud 3 a 16.
* **La unicidad se comprueba sin distinguir mayúsculas**, porque FAT no las distingue: `Tests` y `tests` serían el mismo directorio, y aceptar los dos crea una colisión que nadie sabría diagnosticar.
* Prefijos reservados: `sys-`, y cualquier cosa que empiece por `.`.
* **Estable para siempre.** Cambiar el `id` de una app es una app nueva, y sus datos en `/nitr0s/data/<id>/` quedan huérfanos. El validador avisa de esto explícitamente en vez de dejarlo pasar.

---

## 4. Los ejes de versión, y cuál es el que importa

El brief identificó cinco ejes y señaló cuál es el crítico. Este documento fija los tres que viven en el manifiesto.

* **`api_required`** — el más importante de los cinco. El núcleo expone `API_VERSION` y `API_FLOOR`, los dos enteros. **Un paquete se carga si y solo si `API_FLOOR ≤ api_required ≤ API_VERSION`.** Fuera de rango, el runtime **se niega a cargarlo** y lo dice con el número de los dos lados, en vez de cargarlo y fallar más tarde de forma incomprensible.
* **`pkg_version`** — monótono por app. Gobierna instalación, actualización y vuelta atrás (§1.1). Un paquete con versión menor o igual a la instalada se rechaza salvo que sea una vuelta atrás explícita.
* **Versión del esquema de datos** — por conjunto de datos, dentro de `datasets`. §6.3.

Y el eje que **no** existe: la pantalla. Las capacidades del panel son un **descriptor en tiempo de ejecución**, no una versión y no algo que el manifiesto pueda declarar. Una app que declarase «necesito refresco parcial» estaría congelando en su manifiesto un hecho del hardware que todavía no está comprobado.

---

## 5. Capacidades

Lista **cerrada**. Lo que no está, no existe.

| Capacidad | Qué concede |
| --- | --- |
| `store` | Leer y escribir **sus propios** conjuntos de datos, con cuota. |
| `sync_status` | Leer el enumerado de estado de la sincronización. No dispararla. |
| `sync_force` | Forzar una sincronización. Privilegiada. |
| `power_read` | Leer voltaje de celda, porcentaje y contabilidad de energía. |
| `device_control` | Reiniciar y forzar el sueño. Privilegiada. |
| `radio` | **Pedir** radio. La decisión sigue siendo del árbitro de sueño, que puede negarla. |
| `update` | Disparar una actualización por OTA. Privilegiada. |
| `diag` | Leer registros, últimos fallos y causas de reinicio. |
| `time_status` | Saber si la hora es de fiar, que es un hecho que la app de tests necesita. |

**La regla que hace que esto no sea decorativo:** la tabla de servicios que recibe cada app **se construye según lo concedido**, así que una función no concedida llega **ausente —puntero nulo comprobado al cargar— y no fallando al llamarla**. Una capacidad que faltase y se detectase en la llamada sería un fallo en tiempo de ejecución en el aparato de alguien; así es un rechazo al cargar, en el banco.

Y es lo que hace tratable Ajustes: **«Estado y diagnóstico» es una app con `[power_read, device_control, sync_force, update, diag]`**, no un caso especial tallado a mano en C. Eso convierte en estructuralmente cierto el planteamiento de que las apps son pantallas igual que lo es el propio sistema.

### 5.1. No-capacidades: lo que ninguna app puede hacer nunca

No hay capacidad que las conceda, y no es una lista de omisiones sino de prohibiciones:

* **Dibujar.** Una app devuelve una descripción de vista; nunca recibe primitivas de dibujo. Así la diferencia de rectángulos sucios sale de la estructura de datos y **no se ejecuta código de app en el camino crítico del refresco**.
* **Retener un cerrojo de vigilia.** Los cerrojos pedidos en nombre de una app son **propiedad del sistema** y se liberan cuando el lanzador la abandona. Un fallo de una app nunca debe impedir dormir.
* **Bloquear.** Estrictamente por eventos, con presupuesto de reloj por gancho.
* **Tocar la tarjeta por ruta.** Todo recurso del sistema se entrega **por descriptor, con cierre a cargo del sistema**, nunca como puntero crudo ni como ruta.
* **Llamar a la red.** La sincronización no está en la API de las apps: exponerla invierte la dependencia y destruye en silencio el funcionamiento sin red, porque una app que se olvide de llamarla pierde datos sin avisar. Las apps escriben en `store`, `store` añade al registro, `sync` lo drena.
* **Leer los datos de otra app.**
* **Ver códigos de botón.** Las apps reciben acciones semánticas. El mapeo físico es del módulo de entrada, y usar códigos crudos congelaría dentro de la ABI un hecho de hardware sin confirmar (Q7).

---

## 6. Declaración de esquemas

### 6.1. Un solo punto de autoría

El `manifest.json` —o el `schema.json` de la misma carpeta— es el **único lugar donde un esquema se escribe**. Es elegante porque viaja por el canal de sincronización que ya existe. El registro del servidor **lo ingiere al publicar el paquete y guarda el hash del manifiesto**; es explícitamente derivado y **nunca se edita a mano**. Publicar un paquete cuyo esquema contradiga el hash guardado se rechaza.

### 6.2. El sistema de tipos, deliberadamente diminuto

`string(maxlen)`, `int(min,max)`, `bool`, `enum(valores)`, `timestamp`, `text`. **Seis. Y los tipos de referencia quedan fuera de v1**, porque un tipo de referencia es integridad referencial atravesando una frontera de sincronización sin red, y eso no se arregla después.

Cada campo declara su **`field_id` numérico**, que es exactamente el que viaja en las operaciones de 4b. Tres reglas que lo mantienen utilizable:

* **Inmutable.** Un `field_id` nunca cambia de significado.
* **Nunca se reutiliza**, ni después de retirar un campo.
* **Nunca se renumera.** Igual que la compactación de 4b conserva las versiones, el esquema conserva los identificadores: renumerar convertiría todo el historial en una mentira silenciosa.

Cada conjunto de datos declara su **`owner`**: `device`, `server` o `shared`, que es lo que la fusión de 4b consulta para desempatar.

### 6.3. Migración

* **Aditiva con valor por defecto** dentro de una versión menor. Es el caso normal y no requiere nada.
* **Los cambios destructivos exigen subida mayor**, una migración explícita **hacia delante** al abrir la app, y un **`dataset_id` nuevo**, de modo que los lotes de sincronización viejos dejen de encajar en vez de fusionarse mal (4b §5.1).
* Nunca hacia atrás. Bajar de versión por OTA es un camino de recuperación, así que **los formatos nuevos tienen que seguir siendo legibles por la compilación N-1** mientras la política sea aditiva. Un cambio destructivo rompe eso, y por eso cuesta una subida mayor.

### 6.4. Los dos lados validan, y la consola tiene que ser la cobarde

El aparato valida al cargar y la consola valida al publicar. **La consola nunca debe aceptar datos que el aparato rechazaría**, lo que significa que usa los mismos límites y que, ante cualquier cosa que no pueda demostrar que el aparato acepta, **rechaza**. El motivo es la asimetría de autoridad: como gana el dispositivo, un dato que la consola acepta y el aparato rechaza se pierde en silencio en el lado que manda.

---

## 7. Reglas de rechazo

Un paquete se rechaza, con un mensaje que nombra el campo, si:

1. Falta `manifest_version`, `api_required`, `id`, `pkg_version`, `name` o `capabilities`.
2. `api_required` cae fuera de `[API_FLOOR, API_VERSION]`.
3. `id` incumple §3, o no coincide con el nombre del directorio, o colisiona sin distinguir mayúsculas con otra app instalada.
4. `pkg_version` no coincide con el nombre del directorio de versión, o no es mayor que la instalada sin ser una vuelta atrás explícita.
5. `capabilities` contiene algo que no está en la lista cerrada de §5. **No se ignora lo desconocido:** una capacidad mal escrita se concedería silenciosamente como «ninguna», y la app fallaría en el aparato en vez de en la publicación.
6. El esquema usa un tipo que no está en los seis de §6.2, o un tipo de referencia.
7. Un `field_id` se repite, se renumera o reutiliza uno retirado.
8. El esquema contradice el hash del manifiesto guardado en el registro del servidor.
9. El JSON está truncado o es inválido. **Y este caso tiene una exigencia de comportamiento, no solo de rechazo:** un manifiesto truncado es uno de los casos sin definir que el brief marcó como capaces de hacer indistinguible una tarjeta corrupta de un ladrillo. Tiene que producir una pantalla legible, no un bucle de arranque — y esa pantalla, con Estado y diagnóstico, **está compilada en flash y no lee la tarjeta para arrancar**.

---

## 8. Ejemplo completo

El manifiesto de la app de tests, con el esquema del banco y el conjunto de datos de repasos de `registro-de-repasos-y-planificador.md`:

```json
{
  "manifest_version": 1,
  "api_required": 1,
  "id": "tests",
  "pkg_version": 3,
  "name": "Tests",
  "entry": "app_tests",
  "capabilities": ["store", "sync_status", "time_status"],
  "icon": "icon.1bpp",
  "views": ["views/sesion.json", "views/resultado.json", "views/bancos.json"],
  "texts": { "es": "texts/es.json" },
  "datasets": [
    {
      "name": "banks",
      "owner": "server",
      "dataset_id": 2001,
      "schema_version": { "major": 1, "minor": 0 },
      "fields": [
        { "field_id": 1, "name": "question_id", "type": "int",    "min": 1, "max": 9007199254740991 },
        { "field_id": 2, "name": "bank_id",     "type": "int",    "min": 1, "max": 65535 },
        { "field_id": 3, "name": "enunciado",   "type": "text" },
        { "field_id": 4, "name": "opciones",    "type": "text" },
        { "field_id": 5, "name": "correcta",    "type": "int",    "min": 0, "max": 9 },
        { "field_id": 6, "name": "content_hash","type": "string", "maxlen": 16 },
        { "field_id": 7, "name": "progress_epoch","type": "int",  "min": 0, "max": 4294967295 },
        { "field_id": 8, "name": "retirada",    "type": "bool" }
      ]
    },
    {
      "name": "reviews",
      "owner": "device",
      "dataset_id": 2002,
      "schema_version": { "major": 1, "minor": 0 },
      "append_only": true,
      "format": "registro-de-repasos-y-planificador.md"
    }
  ]
}
```

Tres cosas que el ejemplo deja ver:

* **`capabilities` es corta y aburrida**, que es la señal de que el diseño va bien. La app más compleja de v1 necesita tres.
* **`progress_epoch` y `content_hash` viajan en el contenido**, propiedad del servidor, que es exactamente el mecanismo con el que se resolvió el reinicio de progreso sin que la consola escriba en un flujo que no es suyo.
* **El conjunto `reviews` es `append_only` y remite a su propio documento** en vez de describir sus campos aquí. Un registro de solo añadir con formato binario propio no es una entidad fusionable, y meterlo en la tabla de campos invitaría a tratarlo como tal.

---

## 9. Lo que queda por decidir

| Qué | Dónde se decide | Bloquea |
| --- | --- | --- |
| Tipos de nodo de vista y su semántica | Paso 4c | Los descriptores de `views` |
| Cuotas concretas por app en `store` | Paso 4c | Nada de v1; hace falta un número antes de la primera escritura |
| Qué textos y con qué claves | D7, inventario de textos | Nada |
| El aspecto de todo | Fase visual, sección 12 | Nada del formato |

Y una nota de alcance: `views` y `texts` son opcionales en v1 porque las tres apps van compiladas en el firmware con `entry`. El manifiesto ya los admite para que **la primera app que no esté compilada dentro no necesite un cambio de formato**, que es lo único que el hito aplazado de Lua necesitaría de este documento.
