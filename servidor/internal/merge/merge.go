// Package merge implementa la fusión del registro de operaciones.
//
// Algoritmo especificado en diseno/registro-de-operaciones-y-sincronizacion.md §4.
// "Gana el dispositivo" NO es una política general: es el desempate cuando el
// tiempo lógico empata, que es lo que siempre debió ser.
package merge

import (
	"bytes"
	"sort"

	"github.com/aitorTD/nitr0S/servidor/internal/opfmt"
)

// Owner se declara en el esquema, no en el código (manifiesto §6.2).
type Owner uint8

const (
	OwnerDevice Owner = 1
	OwnerServer Owner = 2
	OwnerShared Owner = 3
)

// Schema dice de quién es cada tipo de entidad.
type Schema interface {
	Owner(entityType uint16) Owner
}

// MapSchema es la implementación trivial.
type MapSchema map[uint16]Owner

func (m MapSchema) Owner(t uint16) Owner {
	if o, ok := m[t]; ok {
		return o
	}
	return OwnerShared
}

// Conflict registra una escritura perdedora. Nunca se descarta en silencio: sin
// esto, la sincronización parece simplemente rota (§4.2).
type Conflict struct {
	Entity  opfmt.EntityID
	FieldID uint16
	Loser   opfmt.Op
	Winner  opfmt.Op
	Reason  string
}

// Entity es el estado fusionado. Los campos guardan la operación ganadora, no
// solo su valor: así la compactación puede reemitirlas exactas (§3.3).
type Entity struct {
	ID       opfmt.EntityID
	Type     uint16
	CreateOp *opfmt.Op
	Fields   map[uint16]opfmt.Op
}

// Deleted lee el campo reservado 0. Un borrado es un valor, no una ausencia.
func (e *Entity) Deleted() bool {
	op, ok := e.Fields[opfmt.FieldDeleted]
	return ok && len(op.Value) > 0 && op.Value[0] == 1
}

// Field devuelve el valor ganador de un campo.
func (e *Entity) Field(id uint16) ([]byte, bool) {
	op, ok := e.Fields[id]
	if !ok {
		return nil, false
	}
	return op.Value, true
}

// Set es el estado fusionado completo más su registro de conflictos.
type Set struct {
	schema    Schema
	entities  map[opfmt.EntityID]*Entity
	seen      map[opfmt.OpID]bool
	Conflicts []Conflict
}

func New(s Schema) *Set {
	return &Set{schema: s, entities: map[opfmt.EntityID]*Entity{}, seen: map[opfmt.OpID]bool{}}
}

func (s *Set) Entity(id opfmt.EntityID) (*Entity, bool) {
	e, ok := s.entities[id]
	return e, ok
}

// Entities devuelve las entidades en orden determinista.
func (s *Set) Entities() []*Entity {
	out := make([]*Entity, 0, len(s.entities))
	for _, e := range s.entities {
		out = append(out, e)
	}
	sort.Slice(out, func(i, j int) bool { return less(out[i].ID, out[j].ID) })
	return out
}

func less(a, b opfmt.EntityID) bool {
	if a.CreatorID != b.CreatorID {
		return a.CreatorID < b.CreatorID
	}
	return a.CreatorSeq < b.CreatorSeq
}

// sideRank vale 1 si el autor está en el lado dueño de la entidad.
func sideRank(o Owner, ak opfmt.AuthorKind) int {
	switch o {
	case OwnerDevice, OwnerShared:
		// Para shared gana el aparato: ahí vive "gana el dispositivo", y solo ahí.
		if ak == opfmt.AuthorDevice {
			return 1
		}
	case OwnerServer:
		if ak == opfmt.AuthorConsole || ak == opfmt.AuthorAgent {
			return 1
		}
	}
	return 0
}

// beats es un orden total estricto, lo que garantiza convergencia sea cual sea
// el orden de llegada de las operaciones.
func beats(a, b opfmt.Op, o Owner) (bool, string) {
	if a.FieldVersion != b.FieldVersion {
		return a.FieldVersion > b.FieldVersion, "versión de campo mayor"
	}
	ra, rb := sideRank(o, a.AuthorKind), sideRank(o, b.AuthorKind)
	if ra != rb {
		return ra > rb, "empate de versión: gana el lado dueño de la entidad"
	}
	if a.AuthorID != b.AuthorID {
		return a.AuthorID < b.AuthorID, "empate de versión y de lado: gana el autor menor"
	}
	return a.Lamport < b.Lamport, "empate total: gana el lamport menor"
}

// Apply fusiona una operación. Es idempotente y conmutativa.
func (s *Set) Apply(op opfmt.Op) {
	// Idempotencia por op_id: un reenvío no duplica (§4.3 E6).
	if s.seen[op.ID()] {
		return
	}
	s.seen[op.ID()] = true

	e, ok := s.entities[op.Entity]
	if !ok {
		e = &Entity{ID: op.Entity, Type: op.EntityType, Fields: map[uint16]opfmt.Op{}}
		s.entities[op.Entity] = e
	}

	if op.Kind == opfmt.OpCreate {
		// Las creaciones nunca son conflicto. Esta única línea salva las
		// escrituras del asistente (§4.2).
		if e.CreateOp == nil {
			cp := op
			e.CreateOp = &cp
			e.Type = op.EntityType
		}
		return
	}

	field := op.FieldID
	if op.Kind == opfmt.OpDelete {
		field = opfmt.FieldDeleted
		op.FieldID = field
		if len(op.Value) == 0 {
			op.Value = []byte{1}
		}
	}

	owner := s.schema.Owner(e.Type)
	cur, exists := e.Fields[field]
	if !exists {
		e.Fields[field] = op
		return
	}
	win, reason := beats(op, cur, owner)
	winner, loser := cur, op
	if win {
		winner, loser = op, cur
		e.Fields[field] = op
	} else {
		_, reason = beats(cur, op, owner)
	}
	// Solo es conflicto si se pierde una escritura DISTINTA. Dos borrados que
	// convergen al mismo valor no son nada que contarle al usuario, y anotarlos
	// llenaría la consola de ruido que parecería un problema.
	if !bytes.Equal(loser.Value, winner.Value) {
		s.Conflicts = append(s.Conflicts, Conflict{e.ID, field, loser, winner, reason})
	}
}

// ApplyAll fusiona un lote.
func (s *Set) ApplyAll(ops []opfmt.Op) {
	for _, op := range ops {
		s.Apply(op)
	}
}

// Compact devuelve el conjunto mínimo de operaciones que reproduce el estado:
// el create y la operación ganadora de cada campo, CON SUS field_version
// ORIGINALES. Renumerarlas convertiría el registro compactado en una mentira
// sobre el orden y rompería la fusión contra el otro lado (§3.3).
func (s *Set) Compact() []opfmt.Op {
	var out []opfmt.Op
	for _, e := range s.Entities() {
		if e.CreateOp != nil {
			out = append(out, *e.CreateOp)
		}
		fields := make([]uint16, 0, len(e.Fields))
		for f := range e.Fields {
			fields = append(fields, f)
		}
		sort.Slice(fields, func(i, j int) bool { return fields[i] < fields[j] })
		for _, f := range fields {
			out = append(out, e.Fields[f])
		}
	}
	return out
}

// Equal compara dos estados fusionados. Es lo que usan las pruebas de
// convergencia y de compactación.
func Equal(a, b *Set) bool {
	ea, eb := a.Entities(), b.Entities()
	if len(ea) != len(eb) {
		return false
	}
	for i := range ea {
		x, y := ea[i], eb[i]
		if x.ID != y.ID || x.Deleted() != y.Deleted() || len(x.Fields) != len(y.Fields) {
			return false
		}
		if (x.CreateOp == nil) != (y.CreateOp == nil) {
			return false
		}
		for f, xo := range x.Fields {
			yo, ok := y.Fields[f]
			if !ok || xo.FieldVersion != yo.FieldVersion || !bytes.Equal(xo.Value, yo.Value) {
				return false
			}
		}
	}
	return true
}
