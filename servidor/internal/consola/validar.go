package consola

// Validación de bancos de preguntas.
//
// Decisión 7 del brief: el generador de CRUD sale de la consola; quedan tres
// formularios escritos a mano y una pantalla de "pega JSON y valida" para los
// bancos. Esto es esa validación.
//
// Y la regla que la gobierna, de manifiesto §6.4: LA CONSOLA NUNCA DEBE ACEPTAR
// DATOS QUE EL APARATO RECHAZARÍA. Como gana el dispositivo, un dato que la
// consola acepta y el aparato rechaza se pierde en silencio en el lado que
// manda. Así que ante la duda, se rechaza.

import (
	"encoding/json"
	"fmt"
	"strings"
	"unicode/utf8"
)

// Límites. Vienen del sistema de tipos del esquema (manifiesto §6.2) y del tope
// de 4 KB por valor de campo (api-del-nucleo §8).
const (
	MaxEnunciado = 1024
	MaxOpcion    = 256
	MaxOpciones  = 10
	MinOpciones  = 2
	MaxValor     = 4096
)

type Pregunta struct {
	QuestionID uint64   `json:"question_id"`
	BankID     uint32   `json:"bank_id"`
	Enunciado  string   `json:"enunciado"`
	Opciones   []string `json:"opciones"`
	Correcta   *int     `json:"correcta"`
}

type Banco struct {
	BankID    uint32     `json:"bank_id"`
	Nombre    string     `json:"nombre"`
	Preguntas []Pregunta `json:"preguntas"`
}

// Validar devuelve TODOS los problemas, no solo el primero: corregir un banco
// de cien preguntas de error en error es inaceptable.
func Validar(crudo []byte, yaUsados map[uint64]bool) (*Banco, []string) {
	var b Banco
	dec := json.NewDecoder(strings.NewReader(string(crudo)))
	dec.DisallowUnknownFields()
	if err := dec.Decode(&b); err != nil {
		return nil, []string{fmt.Sprintf("el JSON no se puede leer: %v", err)}
	}

	var errs []string
	if b.BankID == 0 {
		errs = append(errs, "bank_id es obligatorio y no puede ser 0")
	}
	if b.Nombre == "" {
		errs = append(errs, "nombre es obligatorio")
	}
	if !utf8.ValidString(b.Nombre) {
		errs = append(errs, "nombre no es UTF-8 válido")
	}
	if len(b.Preguntas) == 0 {
		errs = append(errs, "el banco no tiene ninguna pregunta")
	}

	vistos := map[uint64]bool{}
	for i, p := range b.Preguntas {
		donde := fmt.Sprintf("pregunta %d", i+1)

		switch {
		case p.QuestionID == 0:
			errs = append(errs, donde+": question_id es obligatorio y no puede ser 0")
		case vistos[p.QuestionID]:
			// El validador tiene que impedir esto (sección 11.5 del brief).
			errs = append(errs, fmt.Sprintf("%s: question_id %d repetido dentro del banco",
				donde, p.QuestionID))
		case yaUsados[p.QuestionID]:
			errs = append(errs, fmt.Sprintf("%s: question_id %d ya está usado; "+
				"un identificador NO se reutiliza nunca, ni tras borrar la pregunta",
				donde, p.QuestionID))
		default:
			vistos[p.QuestionID] = true
		}

		if p.BankID != b.BankID {
			errs = append(errs, fmt.Sprintf("%s: bank_id %d no coincide con el del banco (%d)",
				donde, p.BankID, b.BankID))
		}
		if p.Enunciado == "" {
			errs = append(errs, donde+": enunciado vacío")
		}
		if n := utf8.RuneCountInString(p.Enunciado); n > MaxEnunciado {
			errs = append(errs, fmt.Sprintf("%s: enunciado de %d caracteres, el tope es %d",
				donde, n, MaxEnunciado))
		}
		if !utf8.ValidString(p.Enunciado) {
			errs = append(errs, donde+": enunciado no es UTF-8 válido")
		}
		if len(p.Opciones) < MinOpciones {
			errs = append(errs, fmt.Sprintf("%s: hacen falta al menos %d opciones", donde, MinOpciones))
		}
		if len(p.Opciones) > MaxOpciones {
			errs = append(errs, fmt.Sprintf("%s: %d opciones, el tope es %d",
				donde, len(p.Opciones), MaxOpciones))
		}
		for j, o := range p.Opciones {
			if o == "" {
				errs = append(errs, fmt.Sprintf("%s: la opción %d está vacía", donde, j+1))
			}
			if n := utf8.RuneCountInString(o); n > MaxOpcion {
				errs = append(errs, fmt.Sprintf("%s: opción %d de %d caracteres, el tope es %d",
					donde, j+1, n, MaxOpcion))
			}
		}
		switch {
		case p.Correcta == nil:
			errs = append(errs, donde+": falta correcta (el índice de la opción correcta)")
		case *p.Correcta < 0 || *p.Correcta >= len(p.Opciones):
			errs = append(errs, fmt.Sprintf("%s: correcta es %d, pero hay %d opciones (0 a %d)",
				donde, *p.Correcta, len(p.Opciones), len(p.Opciones)-1))
		}
		// El enunciado viaja como un valor de campo, así que el tope duro de
		// 4 KB del núcleo también aplica, en bytes y no en caracteres.
		if len(p.Enunciado) > MaxValor {
			errs = append(errs, fmt.Sprintf("%s: el enunciado ocupa %d bytes y el tope del núcleo es %d",
				donde, len(p.Enunciado), MaxValor))
		}
	}
	if len(errs) > 0 {
		return nil, errs
	}
	return &b, nil
}
