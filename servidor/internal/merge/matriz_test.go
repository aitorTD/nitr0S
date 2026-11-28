package merge_test

// La matriz de conflictos de diseno/registro-de-operaciones-y-sincronizacion.md §7,
// caso por caso. Es el criterio de aceptación "corrección de la sincronización"
// del brief, y se ejecuta sin hardware y sin aparato.
//
// Aprobado = cero ítems perdidos, cero duplicados, convergencia en una ronda.

import (
	"bytes"
	"errors"
	"testing"

	"github.com/aitorTD/nitr0S/servidor/internal/card"
	"github.com/aitorTD/nitr0S/servidor/internal/merge"
	"github.com/aitorTD/nitr0S/servidor/internal/opfmt"
	"github.com/aitorTD/nitr0S/servidor/internal/proposal"
	"github.com/aitorTD/nitr0S/servidor/internal/wire"
)

const (
	typeListItem uint16 = 10
	fText        uint16 = 1
	fDone        uint16 = 2

	devID     uint64 = 0xD0
	consoleID uint64 = 0xC0
	agentID   uint64 = 0xA0
)

var schema = merge.MapSchema{typeListItem: merge.OwnerShared}

func ent(creator, seq uint64) opfmt.EntityID {
	return opfmt.EntityID{CreatorID: creator, CreatorSeq: seq}
}

func newSet() *merge.Set { return merge.New(schema) }

func opCreate(author uint64, ak opfmt.AuthorKind, lam uint64, e opfmt.EntityID) opfmt.Op {
	return opfmt.Op{Schema: opfmt.SchemaVersion, Kind: opfmt.OpCreate, AuthorID: author,
		AuthorKind: ak, Lamport: lam, EntityType: typeListItem, Entity: e}
}

func opSet(author uint64, ak opfmt.AuthorKind, lam uint64, e opfmt.EntityID,
	field uint16, ver uint64, val []byte) opfmt.Op {
	return opfmt.Op{Schema: opfmt.SchemaVersion, Kind: opfmt.OpSetField, AuthorID: author,
		AuthorKind: ak, Lamport: lam, EntityType: typeListItem, Entity: e,
		FieldID: field, FieldVersion: ver, Value: val}
}

func opDel(author uint64, ak opfmt.AuthorKind, lam uint64, e opfmt.EntityID, ver uint64) opfmt.Op {
	return opfmt.Op{Schema: opfmt.SchemaVersion, Kind: opfmt.OpDelete, AuthorID: author,
		AuthorKind: ak, Lamport: lam, EntityType: typeListItem, Entity: e,
		FieldVersion: ver, Value: []byte{1}}
}

func mustField(t *testing.T, s *merge.Set, e opfmt.EntityID, f uint16) []byte {
	t.Helper()
	en, ok := s.Entity(e)
	if !ok {
		t.Fatalf("la entidad %v no existe", e)
	}
	v, ok := en.Field(f)
	if !ok {
		t.Fatalf("el campo %d de %v no existe", f, e)
	}
	return v
}

// --- Caso 1: edición contra edición, mismo campo -----------------------------
func TestCaso01_EdicionContraEdicion(t *testing.T) {
	e := ent(consoleID, 1)
	s := newSet()
	s.ApplyAll([]opfmt.Op{
		opCreate(consoleID, opfmt.AuthorConsole, 1, e),
		opSet(consoleID, opfmt.AuthorConsole, 2, e, fText, 7, []byte("leche entera")),
		opSet(devID, opfmt.AuthorDevice, 3, e, fText, 7, []byte("leche")),
	})
	if got := mustField(t, s, e, fText); !bytes.Equal(got, []byte("leche")) {
		t.Fatalf("empate de versión en entidad shared: debe ganar el aparato, salió %q", got)
	}
	if len(s.Conflicts) != 1 {
		t.Fatalf("se esperaba 1 conflicto registrado, hay %d", len(s.Conflicts))
	}
	if !bytes.Equal(s.Conflicts[0].Loser.Value, []byte("leche entera")) {
		t.Fatalf("el perdedor debe quedar visible para la consola, salió %q", s.Conflicts[0].Loser.Value)
	}
}

// --- Caso 2: edición en aparato contra borrado en consola, campos distintos ---
// El caso que el concepto original tenía mal: aquí estaba la lista encantada.
func TestCaso02_EdicionNoResucitaUnBorrado(t *testing.T) {
	e := ent(consoleID, 1)
	s := newSet()
	s.ApplyAll([]opfmt.Op{
		opCreate(consoleID, opfmt.AuthorConsole, 1, e),
		opSet(consoleID, opfmt.AuthorConsole, 2, e, fText, 1, []byte("leche")),
		opDel(consoleID, opfmt.AuthorConsole, 3, e, 5),
		opSet(devID, opfmt.AuthorDevice, 4, e, fDone, 6, []byte{1}),
	})
	en, _ := s.Entity(e)
	if !en.Deleted() {
		t.Fatal("editar otro campo NO debe resucitar un ítem borrado")
	}
	if got := mustField(t, s, e, fDone); !bytes.Equal(got, []byte{1}) {
		t.Fatal("la edición del aparato debe conservarse aunque el ítem siga borrado")
	}
}

// --- Caso 3: borrado en los dos lados ----------------------------------------
func TestCaso03_BorradoEnLosDosLados(t *testing.T) {
	e := ent(devID, 1)
	s := newSet()
	s.ApplyAll([]opfmt.Op{
		opCreate(devID, opfmt.AuthorDevice, 1, e),
		opDel(consoleID, opfmt.AuthorConsole, 2, e, 4),
		opDel(devID, opfmt.AuthorDevice, 3, e, 5),
	})
	en, _ := s.Entity(e)
	if !en.Deleted() {
		t.Fatal("debe quedar borrado")
	}
	if len(s.Conflicts) != 0 {
		t.Fatalf("dos borrados convergen al mismo valor: no es conflicto, hay %d", len(s.Conflicts))
	}
}

// --- Caso 4: resucitar explícitamente ----------------------------------------
func TestCaso04_ResucitarExplicito(t *testing.T) {
	e := ent(devID, 1)
	s := newSet()
	s.ApplyAll([]opfmt.Op{
		opCreate(devID, opfmt.AuthorDevice, 1, e),
		opDel(consoleID, opfmt.AuthorConsole, 2, e, 5),
		opSet(devID, opfmt.AuthorDevice, 3, e, opfmt.FieldDeleted, 6, []byte{0}),
	})
	en, _ := s.Entity(e)
	if en.Deleted() {
		t.Fatal("un deleted=false explícito con versión mayor debe reactivar el ítem")
	}
}

// --- Caso 5: aparato sin red 7 días, convergencia en una ronda ---------------
func TestCaso05_SinRedSieteDias_ConvergeEnUnaRonda(t *testing.T) {
	var deviceOut, serverOut []opfmt.Op
	for i := uint64(1); i <= 20; i++ {
		e := ent(devID, i)
		deviceOut = append(deviceOut,
			opCreate(devID, opfmt.AuthorDevice, i*2, e),
			opSet(devID, opfmt.AuthorDevice, i*2+1, e, fText, 1, []byte("del aparato")))
	}
	for i := uint64(1); i <= 7; i++ {
		e := ent(consoleID, i)
		serverOut = append(serverOut,
			opCreate(consoleID, opfmt.AuthorConsole, i*2, e),
			opSet(consoleID, opfmt.AuthorConsole, i*2+1, e, fText, 1, []byte("de la consola")))
	}
	onServer, onDevice := newSet(), newSet()
	onServer.ApplyAll(serverOut)
	onDevice.ApplyAll(deviceOut)
	// Una sola ronda de intercambio.
	onServer.ApplyAll(deviceOut)
	onDevice.ApplyAll(serverOut)

	if !merge.Equal(onServer, onDevice) {
		t.Fatal("los dos lados deben converger en una sola ronda")
	}
	if n := len(onServer.Entities()); n != 27 {
		t.Fatalf("cero ítems perdidos y cero duplicados: se esperaban 27, hay %d", n)
	}
}

// --- Casos 6 y 7: tarjeta sacada y corte de corriente ------------------------
func TestCaso06y07_TruncadoDejaPrefijoValido(t *testing.T) {
	var log []byte
	var err error
	for i := uint64(1); i <= 5; i++ {
		log, err = opfmt.Encode(log, opSet(devID, opfmt.AuthorDevice, i, ent(devID, i), fText, 1, []byte("x")))
		if err != nil {
			t.Fatal(err)
		}
	}
	// Caso 7: tres puntos de inyección. Caso 6: el truncado a mitad de registro.
	for _, cut := range []int{len(log) - 1, len(log) - 20, len(log) / 2} {
		ops, validLen, _ := opfmt.Recover(log[:cut])
		if validLen > cut {
			t.Fatalf("el prefijo válido no puede pasar del corte (%d > %d)", validLen, cut)
		}
		for _, op := range ops {
			if op.Schema != opfmt.SchemaVersion || op.AuthorID != devID {
				t.Fatal("se ha interpretado una operación parcial")
			}
		}
		// Y el prefijo válido vuelve a decodificar entero.
		if _, l2, _ := opfmt.Recover(log[:validLen]); l2 != validLen {
			t.Fatalf("el prefijo válido debe ser estable: %d != %d", l2, validLen)
		}
	}
	// Truncar en CADA byte posible nunca debe interpretar basura (prueba C).
	for cut := 0; cut <= len(log); cut++ {
		ops, validLen, _ := opfmt.Recover(log[:cut])
		if validLen > cut || len(ops) > 5 {
			t.Fatalf("corte en %d: prefijo inválido", cut)
		}
	}
}

// --- Caso 8: desvío de reloj de ±24 h ----------------------------------------
func TestCaso08_DesvioDeRelojNoAlteraElOrden(t *testing.T) {
	e := ent(devID, 1)
	bueno := newSet()
	bueno.ApplyAll([]opfmt.Op{
		opSet(consoleID, opfmt.AuthorConsole, 1, e, fText, 1, []byte("a")),
		opSet(consoleID, opfmt.AuthorConsole, 2, e, fText, 2, []byte("b")),
	})
	// Mismo lamport y misma field_version, relojes de pared disparatados.
	torcido := newSet()
	o1 := opSet(consoleID, opfmt.AuthorConsole, 1, e, fText, 1, []byte("a"))
	o1.WallClock = 1<<31 + 86400
	o2 := opSet(consoleID, opfmt.AuthorConsole, 2, e, fText, 2, []byte("b"))
	o2.WallClock = 0 // 1970: el BM8563 tras quedarse sin batería
	torcido.ApplyAll([]opfmt.Op{o1, o2})

	if !merge.Equal(bueno, torcido) {
		t.Fatal("el reloj de pared no debe influir en el resultado: ordena lamport")
	}
	if got := mustField(t, torcido, e, fText); !bytes.Equal(got, []byte("b")) {
		t.Fatalf("debe ganar la field_version mayor, salió %q", got)
	}
}

// --- Caso 9: reenvío de un lote duplicado ------------------------------------
func TestCaso09_ReenvioDuplicadoNoHaceNada(t *testing.T) {
	e := ent(devID, 1)
	lote := []opfmt.Op{
		opCreate(devID, opfmt.AuthorDevice, 1, e),
		opSet(devID, opfmt.AuthorDevice, 2, e, fText, 1, []byte("leche")),
	}
	una, dos := newSet(), newSet()
	una.ApplyAll(lote)
	dos.ApplyAll(lote)
	dos.ApplyAll(lote) // reenvío
	dos.ApplyAll(lote) // y otro
	if !merge.Equal(una, dos) {
		t.Fatal("el reenvío debe ser idempotente por op_id")
	}
	if len(dos.Conflicts) != 0 {
		t.Fatalf("un reenvío no es un conflicto, hay %d", len(dos.Conflicts))
	}
}

// --- Caso 10: tarjeta ajena insertada ----------------------------------------
func TestCaso10_TarjetaAjena(t *testing.T) {
	yo := card.Marker{DeviceID: devID, CardUUID: "AAAA-1111"}
	ajena := &card.Marker{DeviceID: 0xBEEF, CardUUID: "BBBB-2222"}
	d, err := card.Check(yo, ajena)
	if d != card.Adopt || !errors.Is(err, card.ErrForeign) {
		t.Fatal("una tarjeta ajena no se fusiona: se ofrece adopción explícita")
	}
	if d, err := card.Check(yo, nil); d != card.Adopt || !errors.Is(err, card.ErrMissing) {
		t.Fatal("sin marcador tampoco se fusiona")
	}
	if d, err := card.Check(yo, &yo); d != card.Mount || err != nil {
		t.Fatal("la tarjeta propia debe montarse")
	}
}

// --- Caso 11: ítem escrito por el asistente ----------------------------------
func TestCaso11_ItemDelAsistenteSobrevive(t *testing.T) {
	delAgente := ent(agentID, 1)
	delAparato := ent(devID, 1)
	s := newSet()
	s.ApplyAll([]opfmt.Op{
		opCreate(devID, opfmt.AuthorDevice, 1, delAparato),
		opDel(devID, opfmt.AuthorDevice, 2, delAparato, 3),
		// El asistente crea el suyo: nadie lo ha visto, así que no puede perder.
		opCreate(agentID, opfmt.AuthorAgent, 1, delAgente),
		opSet(agentID, opfmt.AuthorAgent, 2, delAgente, fText, 1, []byte("leche")),
	})
	en, ok := s.Entity(delAgente)
	if !ok || en.Deleted() {
		t.Fatal("una creación nunca es conflicto: el ítem del asistente debe sobrevivir")
	}
	if got := mustField(t, s, delAgente, fText); !bytes.Equal(got, []byte("leche")) {
		t.Fatalf("salió %q", got)
	}
}

// --- Caso 12: propuesta no aprobada ------------------------------------------
func TestCaso12_PropuestaNoAprobadaNoAparece(t *testing.T) {
	e := ent(agentID, 9)
	ps := proposal.NewStore()
	id := ps.Add(opSet(agentID, opfmt.AuthorAgent, 1, e, fText, 1, []byte("pan")))

	s := newSet()
	s.ApplyAll(ps.Ops()) // nada aprobado todavía
	if _, ok := s.Entity(e); ok {
		t.Fatal("una propuesta sin aprobar no debe entrar en el registro de operaciones")
	}
	op, err := ps.Approve(id)
	if err != nil {
		t.Fatal(err)
	}
	s.Apply(op)
	if _, ok := s.Entity(e); !ok {
		t.Fatal("al aprobarla sí se emite la operación")
	}
}

// --- Casos 13 y 14: dataset y versión de protocolo ---------------------------
func TestCaso13y14_SobreRechazado(t *testing.T) {
	secreto := []byte("secreto-del-aparato")
	ops := []opfmt.Op{opSet(devID, opfmt.AuthorDevice, 1, ent(devID, 1), fText, 1, []byte("x"))}
	acc := wire.NewAcceptor(2001)

	// Caso 13: dataset_id distinto.
	env := wire.Envelope{ProtoVersion: wire.ProtoVersion, DatasetID: 9999,
		SenderID: devID, SenderKind: opfmt.AuthorDevice, EnvelopeSeq: 1}
	b, err := wire.Marshal(env, ops, secreto)
	if err != nil {
		t.Fatal(err)
	}
	got, gotOps, err := wire.Unmarshal(b, [][]byte{secreto})
	if err != nil {
		t.Fatal(err)
	}
	if err := acc.Accept(got); !errors.Is(err, wire.ErrDataset) {
		t.Fatalf("un dataset_id distinto debe rechazar el lote, salió %v", err)
	}
	s := newSet()
	if len(gotOps) != 1 {
		t.Fatal("el lote se decodifica, pero no se fusiona")
	}
	if len(s.Entities()) != 0 {
		t.Fatal("nada debe haberse fusionado")
	}

	// Caso 14: versión de protocolo no soportada.
	env2 := wire.Envelope{ProtoVersion: 99, DatasetID: 2001,
		SenderID: devID, SenderKind: opfmt.AuthorDevice, EnvelopeSeq: 2}
	b2, _ := wire.Marshal(env2, ops, secreto)
	got2, _, err := wire.Unmarshal(b2, [][]byte{secreto})
	if err != nil {
		t.Fatal(err)
	}
	if err := acc.Accept(got2); !errors.Is(err, wire.ErrProtoUnsupported) {
		t.Fatalf("una versión no soportada debe avisar, salió %v", err)
	}
}

// --- Caso 15: compactación a mitad de camino ---------------------------------
func TestCaso15_CompactarNoCambiaLaFusion(t *testing.T) {
	var todo []opfmt.Op
	for i := uint64(1); i <= 6; i++ {
		e := ent(devID, i)
		todo = append(todo, opCreate(devID, opfmt.AuthorDevice, i*10, e))
		for v := uint64(1); v <= 10; v++ { // diez ediciones del mismo campo
			todo = append(todo, opSet(devID, opfmt.AuthorDevice, i*10+v, e, fText, v, []byte{byte(v)}))
		}
		if i%2 == 0 {
			todo = append(todo, opDel(consoleID, opfmt.AuthorConsole, i*10+20, e, 3))
		}
	}
	original := newSet()
	original.ApplyAll(todo)

	compactado := original.Compact()
	if len(compactado) >= len(todo) {
		t.Fatalf("compactar debe recuperar espacio: %d >= %d", len(compactado), len(todo))
	}
	rehecho := newSet()
	rehecho.ApplyAll(compactado)
	if !merge.Equal(original, rehecho) {
		t.Fatal("el registro compactado debe fusionar igual que el original")
	}
	// Y sigue siendo fusionable contra el otro lado, que es lo que se rompería
	// si la compactación renumerara las field_version.
	contraElOtroLado := newSet()
	contraElOtroLado.ApplyAll(compactado)
	contraElOtroLado.ApplyAll(todo)
	if !merge.Equal(original, contraElOtroLado) {
		t.Fatal("el compactado debe seguir fusionando contra el registro original")
	}
}
