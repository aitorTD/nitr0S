// Package proposal implementa las propuestas del asistente.
//
// Una propuesta NO entra nunca en el registro de operaciones: vive aquí hasta
// que el autor la aprueba, y al aprobarla se emite la operación. El registro
// contiene hechos; si contuviera propuestas, todo lector tendría que saber
// distinguirlas y acabaría por no saberlo (§4.4).
package proposal

import (
	"errors"

	"github.com/aitorTD/nitr0S/servidor/internal/opfmt"
)

type State uint8

const (
	Pending State = iota
	Approved
	Rejected
)

type Proposal struct {
	ID    uint64
	Op    opfmt.Op // la operación que se emitiría al aprobarla
	State State
}

var ErrNotApproved = errors.New("proposal: no aprobada")

type Store struct {
	items map[uint64]*Proposal
	next  uint64
}

func NewStore() *Store { return &Store{items: map[uint64]*Proposal{}, next: 1} }

// Add registra una propuesta. No produce ninguna operación.
func (s *Store) Add(op opfmt.Op) uint64 {
	id := s.next
	s.next++
	s.items[id] = &Proposal{ID: id, Op: op, State: Pending}
	return id
}

// Approve devuelve la operación a emitir. Antes de aprobarla, no hay operación.
func (s *Store) Approve(id uint64) (opfmt.Op, error) {
	p, ok := s.items[id]
	if !ok {
		return opfmt.Op{}, errors.New("proposal: no existe")
	}
	p.State = Approved
	return p.Op, nil
}

func (s *Store) Reject(id uint64) {
	if p, ok := s.items[id]; ok {
		p.State = Rejected
	}
}

// Ops devuelve solo las operaciones de las propuestas aprobadas.
func (s *Store) Ops() []opfmt.Op {
	var out []opfmt.Op
	for _, p := range s.items {
		if p.State == Approved {
			out = append(out, p.Op)
		}
	}
	return out
}
