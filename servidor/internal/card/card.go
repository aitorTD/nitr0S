// Package card implementa el marcador de tarjeta.
//
// Otra tarjeta insertada: el marcador con device_id y UUID de la tarjeta no
// coincide, así que NO se fusiona nada y se ofrece adopción explícita. Una
// tarjeta ajena fusionada en silencio sería indetectable después (§3.2).
package card

import "errors"

type Marker struct {
	DeviceID uint64
	CardUUID string
}

var (
	ErrForeign = errors.New("card: tarjeta ajena; hace falta adopción explícita")
	ErrMissing = errors.New("card: sin marcador; hace falta adopción explícita")
)

// Decision es lo que el aparato hace con una tarjeta al montarla.
type Decision uint8

const (
	Mount Decision = iota // es nuestra: se fusiona
	Adopt                 // ajena o sin marcador: se ofrece adopción, no se fusiona
)

// Check compara el marcador de la tarjeta con la identidad del aparato.
func Check(self Marker, found *Marker) (Decision, error) {
	if found == nil {
		return Adopt, ErrMissing
	}
	if found.DeviceID != self.DeviceID || found.CardUUID != self.CardUUID {
		return Adopt, ErrForeign
	}
	return Mount, nil
}
