// pbm2png convierte la salida de 1 bit del simulador a PNG para poder mirarla.
//
// Es solo para verla: lo que el aparato pinta es el PBM, bit a bit.
// Con -escala N agranda cada píxel, para poder juzgar la tipografía.
package main

import (
	"bufio"
	"flag"
	"fmt"
	"image"
	"image/color"
	"image/png"
	"log"
	"os"
	"path/filepath"
	"strings"
)

func main() {
	escala := flag.Int("escala", 1, "píxeles de salida por píxel del panel")
	flag.Parse()
	if flag.NArg() == 0 {
		log.Fatal("uso: pbm2png [-escala N] fichero.pbm...")
	}
	for _, in := range flag.Args() {
		out := strings.TrimSuffix(in, filepath.Ext(in)) + ".png"
		if err := convertir(in, out, *escala); err != nil {
			log.Fatalf("%s: %v", in, err)
		}
		fmt.Println(out)
	}
}

func convertir(in, out string, escala int) error {
	f, err := os.Open(in)
	if err != nil {
		return err
	}
	defer f.Close()
	r := bufio.NewReader(f)

	var magic string
	var w, h int
	if _, err := fmt.Fscan(r, &magic, &w, &h); err != nil {
		return err
	}
	if magic != "P4" {
		return fmt.Errorf("se esperaba un PBM crudo (P4), es %q", magic)
	}
	if _, err := r.ReadByte(); err != nil { // el único byte en blanco tras la cabecera
		return err
	}
	paso := (w + 7) / 8
	datos := make([]byte, paso*h)
	if _, err := io_ReadFull(r, datos); err != nil {
		return err
	}

	img := image.NewGray(image.Rect(0, 0, w*escala, h*escala))
	for y := 0; y < h; y++ {
		for x := 0; x < w; x++ {
			// 1 = tinta = negro, igual que en el lienzo del núcleo.
			v := uint8(255)
			if datos[y*paso+x/8]&(0x80>>(x%8)) != 0 {
				v = 0
			}
			for dy := 0; dy < escala; dy++ {
				for dx := 0; dx < escala; dx++ {
					img.SetGray(x*escala+dx, y*escala+dy, color.Gray{Y: v})
				}
			}
		}
	}
	g, err := os.Create(out)
	if err != nil {
		return err
	}
	defer g.Close()
	return png.Encode(g, img)
}

func io_ReadFull(r *bufio.Reader, b []byte) (int, error) {
	n := 0
	for n < len(b) {
		k, err := r.Read(b[n:])
		n += k
		if err != nil {
			return n, err
		}
	}
	return n, nil
}
