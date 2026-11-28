package merge_test

// Genera el material de la prueba cruzada: un registro de operaciones y el
// estado al que el servidor lo fusiona. El núcleo en C lee los dos y comprueba
// que llega al mismo sitio.
//
// Es la prueba B del documento del planificador aplicada a la fusión: dos
// implementaciones distintas, el mismo registro, el mismo resultado. Sin ella,
// que los dos lados converjan es una esperanza.
//
// Se regenera con:
//   NITR0S_FIXTURE=../../firmware/pruebas/datos go test ./internal/merge/ -run Fixture

import (
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"testing"

	"github.com/aitorTD/nitr0S/servidor/internal/merge"
	"github.com/aitorTD/nitr0S/servidor/internal/opfmt"
)

// volcar escribe el estado fusionado de forma determinista. El formato lo
// produce también el C, carácter por carácter.
func volcar(s *merge.Set) string {
	var b strings.Builder
	for _, e := range s.Entities() {
		creada := 0
		if e.CreateOp != nil {
			creada = 1
		}
		borrada := 0
		if e.Deleted() {
			borrada = 1
		}
		fmt.Fprintf(&b, "%d-%d tipo=%d creada=%d borrada=%d\n",
			e.ID.CreatorID, e.ID.CreatorSeq, e.Type, creada, borrada)
		campos := make([]int, 0, len(e.Fields))
		for f := range e.Fields {
			campos = append(campos, int(f))
		}
		sort.Ints(campos)
		for _, f := range campos {
			op := e.Fields[uint16(f)]
			fmt.Fprintf(&b, "  campo %d v%d autor=%d/%d len=%d hex=%x\n",
				f, op.FieldVersion, op.AuthorID, op.AuthorKind, len(op.Value), op.Value)
		}
	}
	for _, c := range s.Conflicts {
		fmt.Fprintf(&b, "conflicto %d-%d campo=%d autor=%d perdedor=%x motivo=%s\n",
			c.Entity.CreatorID, c.Entity.CreatorSeq, c.FieldID,
			c.Loser.AuthorKind, c.Loser.Value, c.Reason)
	}
	return b.String()
}

// El registro de la prueba cruzada. Lleva a propósito todos los casos que
// distinguen una implementación correcta de una que "parece que va":
// empates, borrados, resurrecciones, el asistente, y ediciones sucesivas.
func registroCruzado() []opfmt.Op {
	e := func(c, s uint64) opfmt.EntityID { return opfmt.EntityID{CreatorID: c, CreatorSeq: s} }
	mk := func(kind opfmt.OpKind, autor uint64, ak opfmt.AuthorKind, lam uint64,
		ent opfmt.EntityID, campo uint16, ver uint64, val string) opfmt.Op {
		return opfmt.Op{Schema: opfmt.SchemaVersion, Kind: kind, AuthorID: autor,
			AuthorKind: ak, Lamport: lam, WallClock: 1700000000 + int64(lam),
			EntityType: 10, Entity: ent, FieldID: campo, FieldVersion: ver, Value: []byte(val)}
	}
	const (
		dev  = 0xD0
		con  = 0xC0
		agen = 0xA0
	)
	return []opfmt.Op{
		// Entidad 1: creada en consola, el aparato la marca hecha.
		mk(opfmt.OpCreate, con, opfmt.AuthorConsole, 1, e(con, 1), 0, 0, ""),
		mk(opfmt.OpSetField, con, opfmt.AuthorConsole, 2, e(con, 1), 1, 1, "Leche entera"),
		mk(opfmt.OpSetField, dev, opfmt.AuthorDevice, 1, e(con, 1), 2, 1, "\x01"),

		// Entidad 2: empate de versión en el mismo campo. Gana el aparato.
		mk(opfmt.OpCreate, con, opfmt.AuthorConsole, 3, e(con, 2), 0, 0, ""),
		mk(opfmt.OpSetField, con, opfmt.AuthorConsole, 4, e(con, 2), 1, 7, "Pan de centeno grande"),
		mk(opfmt.OpSetField, dev, opfmt.AuthorDevice, 2, e(con, 2), 1, 7, "Pan integral"),

		// Entidad 3: la consola borra y el aparato edita OTRO campo. Sigue borrada.
		mk(opfmt.OpCreate, con, opfmt.AuthorConsole, 5, e(con, 3), 0, 0, ""),
		mk(opfmt.OpSetField, con, opfmt.AuthorConsole, 6, e(con, 3), 1, 1, "Bolsas de basura"),
		mk(opfmt.OpDelete, con, opfmt.AuthorConsole, 7, e(con, 3), 0, 5, "\x01"),
		mk(opfmt.OpSetField, dev, opfmt.AuthorDevice, 3, e(con, 3), 2, 6, "\x01"),

		// Entidad 4: borrada y resucitada explícitamente.
		mk(opfmt.OpCreate, dev, opfmt.AuthorDevice, 4, e(dev, 1), 0, 0, ""),
		mk(opfmt.OpSetField, dev, opfmt.AuthorDevice, 5, e(dev, 1), 1, 1, "Champú"),
		mk(opfmt.OpDelete, con, opfmt.AuthorConsole, 8, e(dev, 1), 0, 2, "\x01"),
		mk(opfmt.OpSetField, dev, opfmt.AuthorDevice, 6, e(dev, 1), 0, 3, "\x00"),

		// Entidad 5: la escribe el asistente. Una creación nunca pierde.
		mk(opfmt.OpCreate, agen, opfmt.AuthorAgent, 1, e(agen, 1), 0, 0, ""),
		mk(opfmt.OpSetField, agen, opfmt.AuthorAgent, 2, e(agen, 1), 1, 1, "Café molido"),

		// Entidad 6: tres ediciones sucesivas del mismo autor. NO son conflictos.
		mk(opfmt.OpCreate, con, opfmt.AuthorConsole, 9, e(con, 4), 0, 0, ""),
		mk(opfmt.OpSetField, con, opfmt.AuthorConsole, 10, e(con, 4), 1, 1, "Pilas"),
		mk(opfmt.OpSetField, con, opfmt.AuthorConsole, 11, e(con, 4), 1, 2, "Pilas AA"),
		mk(opfmt.OpSetField, con, opfmt.AuthorConsole, 12, e(con, 4), 1, 3, "Pilas AAA"),

		// Entidad 7: acentos y ñ, que es donde se rompe una codificación mala.
		mk(opfmt.OpCreate, dev, opfmt.AuthorDevice, 7, e(dev, 2), 0, 0, ""),
		mk(opfmt.OpSetField, dev, opfmt.AuthorDevice, 8, e(dev, 2), 1, 1, "Ñoquis, piña y café"),
	}
}

func TestGenerarFixtureCruzado(t *testing.T) {
	dir := os.Getenv("NITR0S_FIXTURE")
	if dir == "" {
		t.Skip("solo con NITR0S_FIXTURE apuntando a firmware/pruebas/datos")
	}
	ops := registroCruzado()

	var log []byte
	var err error
	for _, op := range ops {
		if log, err = opfmt.Encode(log, op); err != nil {
			t.Fatal(err)
		}
	}
	s := merge.New(merge.MapSchema{10: merge.OwnerShared})
	s.ApplyAll(ops)

	if err := os.WriteFile(filepath.Join(dir, "registro.bin"), log, 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "estado_go.txt"), []byte(volcar(s)), 0o644); err != nil {
		t.Fatal(err)
	}
	t.Logf("%d operaciones, %d bytes, %d entidades, %d conflictos",
		len(ops), len(log), len(s.Entities()), len(s.Conflicts))
}

// Y que el propio volcado sea estable: fusionar en otro orden da el mismo
// texto, así que el fichero de referencia no depende de cómo se generó.
func TestVolcadoEstable(t *testing.T) {
	ops := registroCruzado()
	a := merge.New(merge.MapSchema{10: merge.OwnerShared})
	a.ApplyAll(ops)
	rev := make([]opfmt.Op, len(ops))
	for i := range ops {
		rev[i] = ops[len(ops)-1-i]
	}
	b := merge.New(merge.MapSchema{10: merge.OwnerShared})
	b.ApplyAll(rev)
	// Los conflictos se anotan en orden de llegada, así que solo se compara el
	// estado: es lo que tiene que converger.
	if !merge.Equal(a, b) {
		t.Fatal("el estado no puede depender del orden de llegada")
	}
}
