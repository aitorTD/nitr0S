package store_test

import (
	"path/filepath"
	"testing"

	"github.com/aitorTD/nitr0S/servidor/internal/merge"
	"github.com/aitorTD/nitr0S/servidor/internal/opfmt"
	"github.com/aitorTD/nitr0S/servidor/internal/store"
)

func abrir(t *testing.T) *store.DB {
	t.Helper()
	db, err := store.Open(filepath.Join(t.TempDir(), "nitr0s.db"))
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { db.Close() })
	return db
}

func op(author, lam uint64, ak opfmt.AuthorKind, creator, seq uint64, field uint16, ver uint64, v []byte) opfmt.Op {
	return opfmt.Op{Schema: opfmt.SchemaVersion, Kind: opfmt.OpSetField, AuthorID: author,
		AuthorKind: ak, Lamport: lam, EntityType: 10,
		Entity:  opfmt.EntityID{CreatorID: creator, CreatorSeq: seq},
		FieldID: field, FieldVersion: ver, Value: v}
}

// Las operaciones tienen que volver del almacén exactamente como entraron, y
// fusionar al mismo estado: si no, el servidor y el aparato divergen.
func TestIdaYVueltaPorSQLite(t *testing.T) {
	db := abrir(t)
	ops := []opfmt.Op{
		op(0xD0, 1, opfmt.AuthorDevice, 0xD0, 1, 1, 3, []byte("leche")),
		op(0xC0, 1, opfmt.AuthorConsole, 0xD0, 1, 1, 2, []byte("leche entera")),
		op(0xC0, 2, opfmt.AuthorConsole, 0xC0, 1, 1, 1, []byte("pan")),
	}
	n, err := db.PutOps(ops)
	if err != nil {
		t.Fatal(err)
	}
	if n != 3 {
		t.Fatalf("se esperaban 3 nuevas, salieron %d", n)
	}

	schema := merge.MapSchema{10: merge.OwnerShared}
	directo, desdeDB := merge.New(schema), merge.New(schema)
	directo.ApplyAll(ops)
	leidas, err := db.Ops()
	if err != nil {
		t.Fatal(err)
	}
	desdeDB.ApplyAll(leidas)
	if !merge.Equal(directo, desdeDB) {
		t.Fatal("el estado fusionado desde SQLite debe ser idéntico al fusionado en memoria")
	}
}

// El reenvío lo deduplica el almacén, porque la clave primaria ES la clave de
// idempotencia (author_id, lamport).
func TestReenvioDeduplicadoPorLaClavePrimaria(t *testing.T) {
	db := abrir(t)
	ops := []opfmt.Op{op(0xD0, 1, opfmt.AuthorDevice, 0xD0, 1, 1, 1, []byte("x"))}
	if n, err := db.PutOps(ops); err != nil || n != 1 {
		t.Fatalf("primera vez: n=%d err=%v", n, err)
	}
	for i := 0; i < 3; i++ {
		n, err := db.PutOps(ops)
		if err != nil {
			t.Fatal(err)
		}
		if n != 0 {
			t.Fatalf("un reenvío no inserta nada, salió n=%d", n)
		}
	}
	todas, err := db.Ops()
	if err != nil {
		t.Fatal(err)
	}
	if len(todas) != 1 {
		t.Fatalf("debe haber una sola operación, hay %d", len(todas))
	}
}

func TestConflictosYMarcasDeAgua(t *testing.T) {
	db := abrir(t)
	s := merge.New(merge.MapSchema{10: merge.OwnerShared})
	s.ApplyAll([]opfmt.Op{
		op(0xC0, 1, opfmt.AuthorConsole, 0xD0, 1, 1, 7, []byte("leche entera")),
		op(0xD0, 1, opfmt.AuthorDevice, 0xD0, 1, 1, 7, []byte("leche")),
	})
	if len(s.Conflicts) != 1 {
		t.Fatalf("se esperaba 1 conflicto, hay %d", len(s.Conflicts))
	}
	if err := db.PutConflicts(s.Conflicts); err != nil {
		t.Fatal(err)
	}
	if n, err := db.CountConflicts(); err != nil || n != 1 {
		t.Fatalf("n=%d err=%v", n, err)
	}

	if seq, err := db.PeerSeq(0xD0); err != nil || seq != 0 {
		t.Fatalf("un par desconocido empieza en 0: seq=%d err=%v", seq, err)
	}
	if err := db.SetPeerSeq(0xD0, 42); err != nil {
		t.Fatal(err)
	}
	if seq, err := db.PeerSeq(0xD0); err != nil || seq != 42 {
		t.Fatalf("seq=%d err=%v", seq, err)
	}
}

// Clase C3: un evento con schema desconocido se guarda igual y se marca como no
// interpretado. Perderlo sería perder un hecho.
func TestEventosSoloAnadirYSchemaDesconocido(t *testing.T) {
	db := abrir(t)
	if nuevo, err := db.AppendEvent(0xD0, 1, 2002, opfmt.SchemaVersion, []byte("repaso")); err != nil || !nuevo {
		t.Fatalf("nuevo=%v err=%v", nuevo, err)
	}
	if nuevo, err := db.AppendEvent(0xD0, 1, 2002, opfmt.SchemaVersion, []byte("repaso")); err != nil || nuevo {
		t.Fatal("el mismo evento dos veces no se duplica")
	}
	if nuevo, err := db.AppendEvent(0xD0, 2, 2002, 99, []byte("del futuro")); err != nil || !nuevo {
		t.Fatal("un schema desconocido se guarda igual")
	}
	if n, err := db.CountEvents(); err != nil || n != 2 {
		t.Fatalf("n=%d err=%v", n, err)
	}
}
