package merge_test

// Propiedades que sostienen la convergencia. No son casos de la matriz: son lo
// que hace que la matriz valga para cualquier orden de llegada y no solo para
// el que se probó.

import (
	"errors"
	"math/rand"
	"testing"

	"github.com/aitorTD/nitr0S/servidor/internal/merge"
	"github.com/aitorTD/nitr0S/servidor/internal/opfmt"
	"github.com/aitorTD/nitr0S/servidor/internal/wire"
)

// La fusión no puede depender del orden de llegada: sin esto, dos lados que
// reciben lo mismo en distinto orden divergen, y eso no lo detecta ningún caso
// concreto de la matriz.
func TestFusionIndependienteDelOrden(t *testing.T) {
	var ops []opfmt.Op
	autores := []struct {
		id uint64
		k  opfmt.AuthorKind
	}{{devID, opfmt.AuthorDevice}, {consoleID, opfmt.AuthorConsole}, {agentID, opfmt.AuthorAgent}}

	for i := uint64(1); i <= 8; i++ {
		e := ent(autores[i%3].id, i)
		ops = append(ops, opCreate(autores[i%3].id, autores[i%3].k, i, e))
		for v := uint64(1); v <= 4; v++ {
			a := autores[(i+v)%3]
			ops = append(ops, opSet(a.id, a.k, i*100+v, e, fText, v, []byte{byte(v)}))
			ops = append(ops, opSet(a.id, a.k, i*100+v+50, e, fDone, v, []byte{byte(v % 2)}))
		}
		if i%3 == 0 {
			ops = append(ops, opDel(consoleID, opfmt.AuthorConsole, i*100+90, e, 2))
		}
	}

	ref := newSet()
	ref.ApplyAll(ops)

	rng := rand.New(rand.NewSource(1))
	for it := 0; it < 200; it++ {
		mezcla := append([]opfmt.Op(nil), ops...)
		rng.Shuffle(len(mezcla), func(i, j int) { mezcla[i], mezcla[j] = mezcla[j], mezcla[i] })
		otro := newSet()
		otro.ApplyAll(mezcla)
		if !merge.Equal(ref, otro) {
			t.Fatalf("iteración %d: el orden de llegada ha cambiado el resultado", it)
		}
	}
}

// Aplicar dos veces lo mismo no puede cambiar nada.
func TestFusionIdempotente(t *testing.T) {
	e := ent(devID, 1)
	ops := []opfmt.Op{
		opCreate(devID, opfmt.AuthorDevice, 1, e),
		opSet(devID, opfmt.AuthorDevice, 2, e, fText, 3, []byte("leche")),
		opDel(consoleID, opfmt.AuthorConsole, 3, e, 2),
	}
	una, dos := newSet(), newSet()
	una.ApplyAll(ops)
	for i := 0; i < 5; i++ {
		dos.ApplyAll(ops)
	}
	if !merge.Equal(una, dos) {
		t.Fatal("la fusión debe ser idempotente")
	}
}

// El formato tiene que sobrevivir a una ida y vuelta exacta: es el mismo byte a
// byte en la tarjeta, por HTTPS y por rsync.
func TestCodificacionIdaYVuelta(t *testing.T) {
	orig := opfmt.Op{
		Schema: opfmt.SchemaVersion, Kind: opfmt.OpSetField,
		AuthorID: 0x1122334455667788, AuthorKind: opfmt.AuthorAgent,
		Lamport: 0xFFFFFFFFFF, WallClock: -1,
		EntityType: 7, Entity: ent(0xAABB, 0xCCDD),
		FieldID: 0xBEEF, FieldVersion: 0x0102030405060708,
		Value: []byte("acentos: ñ, ¿qué?, ¡sí!"),
	}
	buf, err := opfmt.Encode(nil, orig)
	if err != nil {
		t.Fatal(err)
	}
	got, n, err := opfmt.Decode(buf)
	if err != nil {
		t.Fatal(err)
	}
	if n != len(buf) {
		t.Fatalf("consumidos %d de %d bytes", n, len(buf))
	}
	if got.AuthorID != orig.AuthorID || got.Lamport != orig.Lamport ||
		got.WallClock != orig.WallClock || got.Entity != orig.Entity ||
		got.FieldID != orig.FieldID || got.FieldVersion != orig.FieldVersion ||
		string(got.Value) != string(orig.Value) {
		t.Fatalf("ida y vuelta no exacta:\n%+v\n%+v", orig, got)
	}
}

// Un bit cambiado en cualquier posición tiene que detectarse.
func TestCrcDetectaCorrupcion(t *testing.T) {
	buf, err := opfmt.Encode(nil, opSet(devID, opfmt.AuthorDevice, 1, ent(devID, 1), fText, 1, []byte("leche")))
	if err != nil {
		t.Fatal(err)
	}
	for i := range buf {
		roto := append([]byte(nil), buf...)
		roto[i] ^= 0x01
		if _, _, err := opfmt.Decode(roto); err == nil {
			t.Fatalf("byte %d corrompido y no se ha detectado", i)
		}
	}
}

// El antirreplay: estrictamente mayor, y correcto con un reloj equivocado
// porque no mira ningún reloj. Un reintento usa un envelope_seq nuevo.
func TestAntirreplay(t *testing.T) {
	acc := wire.NewAcceptor(2001)
	base := wire.Envelope{ProtoVersion: wire.ProtoVersion, DatasetID: 2001,
		SenderID: devID, SenderKind: opfmt.AuthorDevice}

	for _, seq := range []uint64{1, 2, 3} {
		e := base
		e.EnvelopeSeq = seq
		if err := acc.Accept(e); err != nil {
			t.Fatalf("seq %d debería aceptarse: %v", seq, err)
		}
	}
	for _, seq := range []uint64{3, 2, 1, 0} {
		e := base
		e.EnvelopeSeq = seq
		if err := acc.Accept(e); !errors.Is(err, wire.ErrReplay) {
			t.Fatalf("seq %d debería rechazarse como replay, salió %v", seq, err)
		}
	}
	// Dos aparatos distintos llevan contadores independientes.
	otro := base
	otro.SenderID = 0xD1
	otro.EnvelopeSeq = 1
	if err := acc.Accept(otro); err != nil {
		t.Fatalf("otro aparato empieza en 1: %v", err)
	}
}

// Un HMAC que no cuadra se rechaza, y la rotación de claves acepta dos.
func TestHmacYRotacion(t *testing.T) {
	viejo, nuevo := []byte("secreto-viejo"), []byte("secreto-nuevo")
	ops := []opfmt.Op{opSet(devID, opfmt.AuthorDevice, 1, ent(devID, 1), fText, 1, []byte("x"))}
	env := wire.Envelope{ProtoVersion: wire.ProtoVersion, DatasetID: 2001,
		SenderID: devID, SenderKind: opfmt.AuthorDevice, EnvelopeSeq: 1}

	b, err := wire.Marshal(env, ops, viejo)
	if err != nil {
		t.Fatal(err)
	}
	if _, _, err := wire.Unmarshal(b, [][]byte{nuevo}); !errors.Is(err, wire.ErrAuth) {
		t.Fatalf("firmado con el viejo y solo acepta el nuevo: debe fallar, salió %v", err)
	}
	// Ventana de solape: los dos valen.
	if _, _, err := wire.Unmarshal(b, [][]byte{nuevo, viejo}); err != nil {
		t.Fatalf("durante la rotación los dos secretos valen: %v", err)
	}
	// Un byte tocado del cuerpo invalida la firma.
	b[20] ^= 0x01
	if _, _, err := wire.Unmarshal(b, [][]byte{viejo}); !errors.Is(err, wire.ErrAuth) {
		t.Fatalf("un cuerpo manipulado debe fallar la firma, salió %v", err)
	}
}

// La versión 0 nunca existió: aceptarla sería aceptar un sobre sin definir.
func TestVersionCeroNoSeAcepta(t *testing.T) {
	acc := wire.NewAcceptor(2001)
	if acc.Supported(0) {
		t.Fatal("la versión de protocolo 0 no debe aceptarse")
	}
	if !acc.Supported(wire.ProtoVersion) {
		t.Fatal("la versión actual sí")
	}
	if got := wire.SupportedVersions(); len(got) != 1 || got[0] != wire.ProtoVersion {
		t.Fatalf("con ProtoVersion=1 solo se habla una versión, salió %v", got)
	}
}
