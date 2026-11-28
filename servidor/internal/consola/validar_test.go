package consola_test

import (
	"strings"
	"testing"

	"github.com/aitorTD/nitr0S/servidor/internal/consola"
)

const bancoBueno = `{
  "bank_id": 1,
  "nombre": "Farmacología, tema 3",
  "preguntas": [
    {"question_id": 1001, "bank_id": 1, "enunciado": "¿Vida media?", "opciones": ["2 h","6 h"], "correcta": 1},
    {"question_id": 1002, "bank_id": 1, "enunciado": "¿Y la ñ?", "opciones": ["sí","no","¡sí!"], "correcta": 0}
  ]
}`

func TestBancoValido(t *testing.T) {
	b, errs := consola.Validar([]byte(bancoBueno), nil)
	if errs != nil {
		t.Fatalf("debería ser válido: %v", errs)
	}
	if b.BankID != 1 || len(b.Preguntas) != 2 {
		t.Fatalf("salió %+v", b)
	}
}

func TestElValidadorEsConservador(t *testing.T) {
	casos := []struct {
		nombre, json, espera string
	}{
		{"json roto", `{"bank_id":`, "no se puede leer"},
		{"campo desconocido", `{"bank_id":1,"nombre":"x","preguntas":[],"colorcito":"azul"}`, "no se puede leer"},
		{"sin bank_id", `{"nombre":"x","preguntas":[{"question_id":1,"bank_id":0,"enunciado":"e","opciones":["a","b"],"correcta":0}]}`, "bank_id es obligatorio"},
		{"sin preguntas", `{"bank_id":1,"nombre":"x","preguntas":[]}`, "no tiene ninguna pregunta"},
		{"id repetido", `{"bank_id":1,"nombre":"x","preguntas":[
			{"question_id":7,"bank_id":1,"enunciado":"a","opciones":["a","b"],"correcta":0},
			{"question_id":7,"bank_id":1,"enunciado":"b","opciones":["a","b"],"correcta":0}]}`, "repetido"},
		{"bank_id que no cuadra", `{"bank_id":1,"nombre":"x","preguntas":[
			{"question_id":7,"bank_id":9,"enunciado":"a","opciones":["a","b"],"correcta":0}]}`, "no coincide"},
		{"una sola opción", `{"bank_id":1,"nombre":"x","preguntas":[
			{"question_id":7,"bank_id":1,"enunciado":"a","opciones":["a"],"correcta":0}]}`, "al menos 2 opciones"},
		{"correcta fuera de rango", `{"bank_id":1,"nombre":"x","preguntas":[
			{"question_id":7,"bank_id":1,"enunciado":"a","opciones":["a","b"],"correcta":5}]}`, "correcta es 5"},
		{"concordancia con una sola opción", `{"bank_id":1,"nombre":"x","preguntas":[
			{"question_id":7,"bank_id":1,"enunciado":"a","opciones":["a"],"correcta":3}]}`, "solo hay 1 opción"},
		{"falta correcta", `{"bank_id":1,"nombre":"x","preguntas":[
			{"question_id":7,"bank_id":1,"enunciado":"a","opciones":["a","b"]}]}`, "falta correcta"},
		{"opción vacía", `{"bank_id":1,"nombre":"x","preguntas":[
			{"question_id":7,"bank_id":1,"enunciado":"a","opciones":["a",""],"correcta":0}]}`, "está vacía"},
		{"enunciado vacío", `{"bank_id":1,"nombre":"x","preguntas":[
			{"question_id":7,"bank_id":1,"enunciado":"","opciones":["a","b"],"correcta":0}]}`, "enunciado vacío"},
	}
	for _, c := range casos {
		t.Run(c.nombre, func(t *testing.T) {
			b, errs := consola.Validar([]byte(c.json), nil)
			if errs == nil {
				t.Fatalf("debería rechazarse, pero salió %+v", b)
			}
			if !strings.Contains(strings.Join(errs, " | "), c.espera) {
				t.Fatalf("se esperaba un error con %q, salieron: %v", c.espera, errs)
			}
		})
	}
}

// Un question_id ya usado no se puede reutilizar NUNCA, ni tras borrar la
// pregunta: el progreso de repetición espaciada va atado a él.
func TestNoSeReutilizaUnQuestionId(t *testing.T) {
	_, errs := consola.Validar([]byte(bancoBueno), map[uint64]bool{1001: true})
	if errs == nil {
		t.Fatal("debería rechazar un question_id ya usado")
	}
	if !strings.Contains(strings.Join(errs, " "), "ya está usado") {
		t.Fatalf("salieron: %v", errs)
	}
}

// Se listan TODOS los problemas: corregir un banco de error en error es
// inaceptable.
func TestSeListanTodosLosProblemas(t *testing.T) {
	malo := `{"bank_id":0,"nombre":"","preguntas":[
		{"question_id":0,"bank_id":9,"enunciado":"","opciones":["a"],"correcta":9}]}`
	_, errs := consola.Validar([]byte(malo), nil)
	if len(errs) < 5 {
		t.Fatalf("se esperaban al menos 5 problemas listados, salieron %d: %v", len(errs), errs)
	}
}
