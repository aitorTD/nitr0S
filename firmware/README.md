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
| `pruebas/` — 34 comprobaciones | en verde |
| Anfitrión de `app_t`, bucle de eventos y presupuestos | pendiente |
| Las tres apps de v1 | pendientes |
| Capa de abstracción de hardware, panel, sueño | pendiente, necesita el aparato |

## Usar

```sh
make            # compila el simulador y pasa las pruebas
./nitr0sim .    # escribe un .pbm por maqueta y pantalla
go run ../herramientas/pbm2png -escala 2 *.pbm
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
