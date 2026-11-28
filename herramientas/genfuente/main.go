// genfuente rasteriza una fuente TrueType a una tabla de glifos de 1 bit para
// el núcleo de nitr0S.
//
// Por qué existe: §12.1 del brief fija que el texto se pinta con fuentes de
// MAPA DE BITS pre-renderizadas en un conjunto fijo de tamaños incrustadas en
// flash, y que no hay grises. Esta herramienta es ese pre-renderizado, y
// convierte la elección de familia y tamaños en un cambio de DATOS, no de
// código: la fase visual podrá probar tipografías sin tocar el renderizador.
//
// Uso:
//
//	go run ./genfuente -ttf <fichero.ttf> -px 16 -nombre fuente16 \
//	  -salida ../firmware/nucleo/fuente_fuente16.c
package main

import (
	"flag"
	"fmt"
	"image"
	"log"
	"os"
	"sort"
	"strings"

	"golang.org/x/image/font"
	"golang.org/x/image/font/opentype"
	"golang.org/x/image/math/fixed"
)

// Cobertura obligatoria: ASCII imprimible más lo que el español necesita.
// §12.1: acentos, ñ, ¿ y ¡ son cobertura obligatoria.
func runas() []rune {
	var rs []rune
	for r := rune(32); r <= 126; r++ {
		rs = append(rs, r)
	}
	rs = append(rs, []rune("áéíóúüñÁÉÍÓÚÜÑ¿¡«»·—…º°€")...)
	sort.Slice(rs, func(i, j int) bool { return rs[i] < rs[j] })
	return rs
}

type glifo struct {
	r              rune
	w, h, xoff, yo int
	avance         int
	bits           []byte // filas empaquetadas, 1 bit por píxel
}

func main() {
	ttf := flag.String("ttf", "", "fichero TrueType de origen")
	px := flag.Float64("px", 16, "tamaño en píxeles")
	nombre := flag.String("nombre", "fuente16", "nombre del símbolo en C")
	salida := flag.String("salida", "", "fichero .c de salida")
	origen := flag.String("origen", "", "qué poner en el comentario de procedencia")
	flag.Parse()
	if *ttf == "" || *salida == "" {
		log.Fatal("hacen falta -ttf y -salida")
	}

	raw, err := os.ReadFile(*ttf)
	if err != nil {
		log.Fatal(err)
	}
	f, err := opentype.Parse(raw)
	if err != nil {
		log.Fatal(err)
	}
	cara, err := opentype.NewFace(f, &opentype.FaceOptions{
		Size: *px, DPI: 72,
		// Sin antialiasing: el panel es de 1 bit y §12.1 dice que ni grises ni
		// trazos finos para nada importante.
		Hinting: font.HintingFull,
	})
	if err != nil {
		log.Fatal(err)
	}
	defer cara.Close()

	m := cara.Metrics()
	alto := (m.Ascent + m.Descent).Ceil()
	ascenso := m.Ascent.Ceil()

	var gs []glifo
	for _, r := range runas() {
		dr, mask, maskp, avance, ok := cara.Glyph(fixed.P(0, 0), r)
		if !ok {
			log.Fatalf("la fuente no tiene el glifo %q (U+%04X); el español lo necesita", r, r)
		}
		w, h := dr.Dx(), dr.Dy()
		g := glifo{r: r, w: w, h: h, xoff: dr.Min.X, yo: dr.Min.Y, avance: avance.Ceil()}
		if w > 0 && h > 0 {
			paso := (w + 7) / 8
			g.bits = make([]byte, paso*h)
			for y := 0; y < h; y++ {
				for x := 0; x < w; x++ {
					_, _, _, a := mask.At(maskp.X+x, maskp.Y+y).RGBA()
					// Umbral a la mitad: binarizar, no difuminar.
					if a >= 0x8000 {
						g.bits[y*paso+x/8] |= 0x80 >> (x % 8)
					}
				}
			}
		}
		gs = append(gs, g)
	}

	var b strings.Builder
	fmt.Fprintf(&b, "// GENERADO POR herramientas/genfuente. No editar a mano.\n//\n")
	fmt.Fprintf(&b, "// Tamaño: %gpx. Alto de línea: %d. Ascenso: %d. Glifos: %d.\n", *px, alto, ascenso, len(gs))
	if *origen != "" {
		fmt.Fprintf(&b, "// Procedencia: %s\n", *origen)
	}
	fmt.Fprintf(&b, "//\n// ESTA FUENTE ES UN MARCADOR DE POSICIÓN, NO UNA PROPUESTA VISUAL.\n")
	fmt.Fprintf(&b, "// La familia, los pesos y los tamaños son decisiones del autor (brief §12.2)\n")
	fmt.Fprintf(&b, "// y se cambian regenerando este fichero, sin tocar el renderizador.\n\n")
	fmt.Fprintf(&b, "#include \"fuente.h\"\n\n")

	fmt.Fprintf(&b, "static const uint8_t %s_bits[] = {\n", *nombre)
	off := 0
	offs := make([]int, len(gs))
	col := 0
	for i := range gs {
		offs[i] = off
		for _, by := range gs[i].bits {
			if col == 0 {
				b.WriteString("    ")
			}
			fmt.Fprintf(&b, "0x%02X,", by)
			col++
			if col == 16 {
				b.WriteString("\n")
				col = 0
			}
		}
		off += len(gs[i].bits)
	}
	if col != 0 {
		b.WriteString("\n")
	}
	if off == 0 {
		b.WriteString("    0,\n")
	}
	b.WriteString("};\n\n")

	fmt.Fprintf(&b, "static const n0_glifo_t %s_glifos[] = {\n", *nombre)
	for i, g := range gs {
		fmt.Fprintf(&b, "    {0x%04X, %d, %d, %d, %d, %d, %d},  // %s\n",
			g.r, g.w, g.h, g.xoff, g.yo, g.avance, offs[i], escapa(g.r))
	}
	b.WriteString("};\n\n")

	fmt.Fprintf(&b, "const n0_fuente_t %s = {\n", *nombre)
	fmt.Fprintf(&b, "    .nombre = \"%s\",\n", *nombre)
	fmt.Fprintf(&b, "    .alto_linea = %d,\n", alto)
	fmt.Fprintf(&b, "    .ascenso = %d,\n", ascenso)
	fmt.Fprintf(&b, "    .n_glifos = %d,\n", len(gs))
	fmt.Fprintf(&b, "    .glifos = %s_glifos,\n", *nombre)
	fmt.Fprintf(&b, "    .bits = %s_bits,\n", *nombre)
	b.WriteString("};\n")

	if err := os.WriteFile(*salida, []byte(b.String()), 0o644); err != nil {
		log.Fatal(err)
	}
	total := off + len(gs)*int(unsafeSizeofGlifo)
	fmt.Printf("%s: %d glifos, %d bytes de mapas, ~%d bytes en total, alto de línea %d\n",
		*nombre, len(gs), off, total, alto)
}

// Tamaño aproximado de n0_glifo_t para informar del coste en flash.
const unsafeSizeofGlifo = 12

// escapa prepara la runa para ir dentro de un COMENTARIO de C. Una barra
// invertida al final de un comentario de línea lo continúa a la línea
// siguiente y se come el glifo de abajo, así que no se escapa: se nombra.
func escapa(r rune) string {
	if r == '\\' {
		return "U+005C barra invertida"
	}
	if r < 32 {
		return fmt.Sprintf("U+%04X", r)
	}
	return string(r)
}

var _ = image.Rect
