// Package wire implementa el sobre, la autenticación y el antirreplay.
// Especificado en diseno/registro-de-operaciones-y-sincronizacion.md §5.
package wire

import (
	"crypto/hmac"
	"crypto/sha256"
	"encoding/binary"
	"errors"

	"github.com/aitorTD/nitr0S/servidor/internal/opfmt"
)

const Magic = "nitr0Sop"

// ProtoVersion que ofrece este servidor. Soporta N y N-1 (§5.3).
const ProtoVersion uint16 = 1

const headerLen = 8 + 2 + 8 + 8 + 1 + 8 + 4 + 8
const hmacLen = 32

type Envelope struct {
	ProtoVersion uint16
	DatasetID    uint64
	SenderID     uint64
	SenderKind   opfmt.AuthorKind
	EnvelopeSeq  uint64
	OpCount      uint32
	AckedThrough uint64
}

var (
	ErrMagic            = errors.New("wire: no es un lote de nitr0S")
	ErrShort            = errors.New("wire: sobre truncado")
	ErrAuth             = errors.New("wire: HMAC no válido")
	ErrProtoUnsupported = errors.New("wire: versión de protocolo no soportada")
	ErrDataset          = errors.New("wire: dataset_id distinto")
	ErrReplay           = errors.New("wire: envelope_seq no es estrictamente mayor")
	ErrCount            = errors.New("wire: op_count no cuadra")
)

// Marshal serializa el sobre y firma el conjunto con el secreto del aparato.
func Marshal(env Envelope, ops []opfmt.Op, secret []byte) ([]byte, error) {
	buf := make([]byte, headerLen)
	copy(buf[0:], Magic)
	binary.LittleEndian.PutUint16(buf[8:], env.ProtoVersion)
	binary.LittleEndian.PutUint64(buf[10:], env.DatasetID)
	binary.LittleEndian.PutUint64(buf[18:], env.SenderID)
	buf[26] = byte(env.SenderKind)
	binary.LittleEndian.PutUint64(buf[27:], env.EnvelopeSeq)
	binary.LittleEndian.PutUint32(buf[35:], uint32(len(ops)))
	binary.LittleEndian.PutUint64(buf[39:], env.AckedThrough)
	var err error
	for _, op := range ops {
		if buf, err = opfmt.Encode(buf, op); err != nil {
			return nil, err
		}
	}
	m := hmac.New(sha256.New, secret)
	m.Write(buf)
	return append(buf, m.Sum(nil)...), nil
}

// Unmarshal verifica el HMAC contra los secretos válidos. Se aceptan dos
// durante la ventana de solape de la rotación de claves (§5.2).
func Unmarshal(b []byte, secrets [][]byte) (Envelope, []opfmt.Op, error) {
	var env Envelope
	if len(b) < headerLen+hmacLen {
		return env, nil, ErrShort
	}
	if string(b[:8]) != Magic {
		return env, nil, ErrMagic
	}
	body, mac := b[:len(b)-hmacLen], b[len(b)-hmacLen:]
	ok := false
	for _, s := range secrets {
		m := hmac.New(sha256.New, s)
		m.Write(body)
		if hmac.Equal(m.Sum(nil), mac) {
			ok = true
			break
		}
	}
	if !ok {
		return env, nil, ErrAuth
	}
	env.ProtoVersion = binary.LittleEndian.Uint16(body[8:])
	env.DatasetID = binary.LittleEndian.Uint64(body[10:])
	env.SenderID = binary.LittleEndian.Uint64(body[18:])
	env.SenderKind = opfmt.AuthorKind(body[26])
	env.EnvelopeSeq = binary.LittleEndian.Uint64(body[27:])
	env.OpCount = binary.LittleEndian.Uint32(body[35:])
	env.AckedThrough = binary.LittleEndian.Uint64(body[39:])

	ops, _, unparsed := opfmt.Recover(body[headerLen:])
	if uint32(len(ops))+uint32(unparsed) != env.OpCount {
		return env, nil, ErrCount
	}
	return env, ops, nil
}

// Acceptor guarda el último envelope_seq visto por aparato. Es el antirreplay, y
// es correcto con un reloj arbitrariamente equivocado; además no tiene tamaño,
// al contrario que una caché de nonces, que sin límite es una fuga lenta en una
// Pi de 1 GB y ausente no sirve de nada (§5.2).
type Acceptor struct {
	DatasetID uint64
	lastSeq   map[uint64]uint64
}

func NewAcceptor(datasetID uint64) *Acceptor {
	return &Acceptor{DatasetID: datasetID, lastSeq: map[uint64]uint64{}}
}

// Supported dice si esta versión de protocolo se acepta: N y N-1.
func (a *Acceptor) Supported(v uint16) bool {
	// Con ProtoVersion == 1 no hay N-1: la versión 0 nunca existió, y aceptarla
	// sería aceptar un sobre que nadie ha definido.
	return v == ProtoVersion || (ProtoVersion > 1 && v == ProtoVersion-1)
}

// SupportedVersions es lo que el servidor puede decir que habla.
func SupportedVersions() []uint16 {
	if ProtoVersion > 1 {
		return []uint16{ProtoVersion - 1, ProtoVersion}
	}
	return []uint16{ProtoVersion}
}

// Accept comprueba protocolo, dataset y antirreplay. No mira el reloj.
func (a *Acceptor) Accept(env Envelope) error {
	if !a.Supported(env.ProtoVersion) {
		return ErrProtoUnsupported
	}
	if env.DatasetID != a.DatasetID {
		return ErrDataset
	}
	if env.EnvelopeSeq <= a.lastSeq[env.SenderID] {
		return ErrReplay
	}
	a.lastSeq[env.SenderID] = env.EnvelopeSeq
	return nil
}

// PeekSender lee el sender_id del sobre SIN verificar la firma. Hace falta para
// saber qué secretos probar, y es seguro porque nada se acepta hasta que el
// HMAC cuadra: un sender_id mentido solo lleva a que la firma falle.
func PeekSender(b []byte) (uint64, error) {
	if len(b) < headerLen+hmacLen {
		return 0, ErrShort
	}
	if string(b[:8]) != Magic {
		return 0, ErrMagic
	}
	return binary.LittleEndian.Uint64(b[18:]), nil
}

// Supported es la comprobación de versión sin necesitar un Acceptor, para los
// servidores que persisten el antirreplay en su propio almacén.
func Supported(v uint16) bool {
	return v == ProtoVersion || (ProtoVersion > 1 && v == ProtoVersion-1)
}
