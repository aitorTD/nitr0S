// Package opfmt implementa la codificación canónica del registro de operaciones.
//
// Formato especificado en diseno/registro-de-operaciones-y-sincronizacion.md §3.
// El mismo byte a byte en la tarjeta, por HTTPS y por rsync: no hay un formato
// de fichero y otro de red (decisión 11 del brief).
package opfmt

import (
	"encoding/binary"
	"errors"
	"fmt"
	"hash/crc32"
)

// SchemaVersion del formato del registro. Solo hacia delante y solo aditiva.
const SchemaVersion uint8 = 1

// FieldDeleted es el campo reservado que hace del borrado un valor y no una
// ausencia, que es lo que permite compararlo (§4.1).
const FieldDeleted uint16 = 0

type OpKind uint8

const (
	OpCreate   OpKind = 1
	OpSetField OpKind = 2
	OpDelete   OpKind = 3
)

type AuthorKind uint8

const (
	AuthorDevice  AuthorKind = 1
	AuthorConsole AuthorKind = 2
	AuthorAgent   AuthorKind = 3
)

// EntityID es el par (creator_id, creator_seq) de §2.1: único por construcción
// y no por probabilidad, sin necesitar entropía ni reloj.
type EntityID struct {
	CreatorID  uint64
	CreatorSeq uint64
}

func (e EntityID) String() string { return fmt.Sprintf("%d:%d", e.CreatorID, e.CreatorSeq) }

// Op es una operación. Campos en el orden de §3.1.
type Op struct {
	Schema       uint8
	Kind         OpKind
	AuthorID     uint64
	AuthorKind   AuthorKind
	Lamport      uint64
	WallClock    int64 // solo para mostrar; nunca ordena nada
	EntityType   uint16
	Entity       EntityID
	FieldID      uint16
	FieldVersion uint64
	Value        []byte
}

// OpID es la identidad de la operación y a la vez la clave de idempotencia de
// la subida: un solo concepto que mantener de acuerdo (§2.1).
type OpID struct {
	AuthorID uint64
	Lamport  uint64
}

func (o Op) ID() OpID { return OpID{o.AuthorID, o.Lamport} }

// payloadLen es el tamaño fijo de la carga sin el valor.
const payloadLen = 1 + 1 + 8 + 1 + 8 + 8 + 2 + 16 + 2 + 8 + 2

// MaxValueLen acota el valor de un campo (api-del-nucleo.md §8).
const MaxValueLen = 4096

var (
	ErrShort     = errors.New("opfmt: registro truncado")
	ErrCRC       = errors.New("opfmt: crc32 no cuadra")
	ErrValueLen  = errors.New("opfmt: valor demasiado largo")
	ErrSchemaNew = errors.New("opfmt: schema más nuevo que el soportado")
)

// Encode escribe la operación con su marco: len u32, carga, crc32 sobre los dos.
func Encode(dst []byte, op Op) ([]byte, error) {
	if len(op.Value) > MaxValueLen {
		return nil, ErrValueLen
	}
	n := payloadLen + len(op.Value)
	buf := make([]byte, 4+n+4)
	binary.LittleEndian.PutUint32(buf[0:], uint32(n))
	p := buf[4 : 4+n]
	p[0] = op.Schema
	p[1] = byte(op.Kind)
	binary.LittleEndian.PutUint64(p[2:], op.AuthorID)
	p[10] = byte(op.AuthorKind)
	binary.LittleEndian.PutUint64(p[11:], op.Lamport)
	binary.LittleEndian.PutUint64(p[19:], uint64(op.WallClock))
	binary.LittleEndian.PutUint16(p[27:], op.EntityType)
	binary.LittleEndian.PutUint64(p[29:], op.Entity.CreatorID)
	binary.LittleEndian.PutUint64(p[37:], op.Entity.CreatorSeq)
	binary.LittleEndian.PutUint16(p[45:], op.FieldID)
	binary.LittleEndian.PutUint64(p[47:], op.FieldVersion)
	binary.LittleEndian.PutUint16(p[55:], uint16(len(op.Value)))
	copy(p[57:], op.Value)
	binary.LittleEndian.PutUint32(buf[4+n:], crc32.ChecksumIEEE(buf[:4+n]))
	return append(dst, buf...), nil
}

// Decode lee una operación y devuelve cuántos bytes consumió.
func Decode(b []byte) (Op, int, error) {
	var op Op
	if len(b) < 8 {
		return op, 0, ErrShort
	}
	n := int(binary.LittleEndian.Uint32(b))
	if n < payloadLen || n > payloadLen+MaxValueLen {
		return op, 0, ErrShort
	}
	total := 4 + n + 4
	if len(b) < total {
		return op, 0, ErrShort
	}
	if crc32.ChecksumIEEE(b[:4+n]) != binary.LittleEndian.Uint32(b[4+n:]) {
		return op, 0, ErrCRC
	}
	p := b[4 : 4+n]
	op.Schema = p[0]
	if op.Schema > SchemaVersion {
		// Se guarda igual y se marca como no interpretado (§8); quien llama decide.
		return op, total, ErrSchemaNew
	}
	op.Kind = OpKind(p[1])
	op.AuthorID = binary.LittleEndian.Uint64(p[2:])
	op.AuthorKind = AuthorKind(p[10])
	op.Lamport = binary.LittleEndian.Uint64(p[11:])
	op.WallClock = int64(binary.LittleEndian.Uint64(p[19:]))
	op.EntityType = binary.LittleEndian.Uint16(p[27:])
	op.Entity.CreatorID = binary.LittleEndian.Uint64(p[29:])
	op.Entity.CreatorSeq = binary.LittleEndian.Uint64(p[37:])
	op.FieldID = binary.LittleEndian.Uint16(p[45:])
	op.FieldVersion = binary.LittleEndian.Uint64(p[47:])
	vlen := int(binary.LittleEndian.Uint16(p[55:]))
	if 57+vlen > len(p) {
		return op, 0, ErrShort
	}
	op.Value = append([]byte(nil), p[57:57+vlen]...)
	return op, total, nil
}

// Recover implementa la pasada de recuperación al arrancar: recorre validando y
// trunca hasta el último registro válido (§3.2). Un corte de corriente o una
// extracción de la tarjeta dejan un registro roto al final, que es este caso.
func Recover(log []byte) (ops []Op, validLen int, unparsed int) {
	off := 0
	for off < len(log) {
		op, n, err := Decode(log[off:])
		if err == ErrSchemaNew {
			unparsed++
			off += n
			validLen = off
			continue
		}
		if err != nil {
			break
		}
		ops = append(ops, op)
		off += n
		validLen = off
	}
	return ops, validLen, unparsed
}
