// Package store es la persistencia del servidor: SQLite en Go puro, sin cgo,
// para que compile cruzado a la Pi desde cualquier sitio con una variable de
// entorno y sin compilar nunca EN la Pi (paso 2 de la cola de arranque).
package store

import (
	"database/sql"
	"fmt"
	"time"

	_ "modernc.org/sqlite"

	"github.com/aitorTD/nitr0S/servidor/internal/merge"
	"github.com/aitorTD/nitr0S/servidor/internal/opfmt"
)

const SchemaVersion = 1

// pragmas. synchronous=NORMAL y no FULL, que era un desacuerdo resuelto del
// brief: en una Pi sin SAI, y siendo la regla de conflicto "gana el
// dispositivo", una escritura perdida de la consola es benigna por
// construcción. WAL por la misma razón, y busy_timeout porque la consola y la
// sincronización escriben a la vez.
const pragmas = `
PRAGMA journal_mode=WAL;
PRAGMA synchronous=NORMAL;
PRAGMA busy_timeout=5000;
PRAGMA foreign_keys=ON;
`

const schema = `
CREATE TABLE IF NOT EXISTS meta (
  k TEXT PRIMARY KEY,
  v TEXT NOT NULL
);

-- La clave primaria ES la clave de idempotencia (author_id, lamport), así que
-- un reenvío lo deduplica el almacén y no solo el motor de fusión.
CREATE TABLE IF NOT EXISTS ops (
  author_id      INTEGER NOT NULL,
  lamport        INTEGER NOT NULL,
  author_kind    INTEGER NOT NULL,
  kind           INTEGER NOT NULL,
  entity_creator INTEGER NOT NULL,
  entity_seq     INTEGER NOT NULL,
  entity_type    INTEGER NOT NULL,
  field_id       INTEGER NOT NULL,
  field_version  INTEGER NOT NULL,
  wall_clock     INTEGER NOT NULL,
  value          BLOB,
  PRIMARY KEY (author_id, lamport)
) WITHOUT ROWID;

CREATE INDEX IF NOT EXISTS ops_entity ON ops (entity_creator, entity_seq, field_id);

-- Marcas de agua por par. En el aparato viven en NVS, porque la decisión de
-- sincronizar al cogerlo se toma antes de montar la SD; aquí en la Pi no hay
-- esa restricción.
CREATE TABLE IF NOT EXISTS peers (
  peer_id           INTEGER PRIMARY KEY,
  last_envelope_seq INTEGER NOT NULL DEFAULT 0,
  received_through  INTEGER NOT NULL DEFAULT 0,
  acked_through     INTEGER NOT NULL DEFAULT 0,
  last_seen         INTEGER NOT NULL DEFAULT 0
);

-- Nunca se descarta en silencio una escritura perdedora.
CREATE TABLE IF NOT EXISTS conflicts (
  id             INTEGER PRIMARY KEY AUTOINCREMENT,
  entity_creator INTEGER NOT NULL,
  entity_seq     INTEGER NOT NULL,
  field_id       INTEGER NOT NULL,
  reason         TEXT NOT NULL,
  loser_author   INTEGER NOT NULL,
  loser_kind     INTEGER NOT NULL,
  loser_value    BLOB,
  winner_value   BLOB,
  created_at     INTEGER NOT NULL,
  acknowledged   INTEGER NOT NULL DEFAULT 0
);

-- Clase C3: eventos que solo se añaden, con clave de idempotencia. Se guardan
-- TAL CUAL y se interpretan después; un evento con schema desconocido se
-- guarda igual y se marca, porque perderlo sería perder un hecho.
CREATE TABLE IF NOT EXISTS events (
  author_id  INTEGER NOT NULL,
  lamport    INTEGER NOT NULL,
  dataset_id INTEGER NOT NULL,
  schema     INTEGER NOT NULL,
  payload    BLOB NOT NULL,
  received_at INTEGER NOT NULL,
  interpreted INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY (author_id, lamport)
) WITHOUT ROWID;
`

type DB struct{ sql *sql.DB }

func Open(path string) (*DB, error) {
	h, err := sql.Open("sqlite", path)
	if err != nil {
		return nil, err
	}
	if _, err := h.Exec(pragmas); err != nil {
		h.Close()
		return nil, fmt.Errorf("pragmas: %w", err)
	}
	if _, err := h.Exec(schema); err != nil {
		h.Close()
		return nil, fmt.Errorf("esquema: %w", err)
	}
	db := &DB{sql: h}
	if _, err := h.Exec(`INSERT OR IGNORE INTO meta (k,v) VALUES ('schema_version', ?)`,
		fmt.Sprint(SchemaVersion)); err != nil {
		h.Close()
		return nil, err
	}
	return db, nil
}

func (d *DB) Close() error { return d.sql.Close() }

// PutOps guarda un lote. Devuelve cuántas eran nuevas: un reenvío da 0 sin
// error, que es la idempotencia vista desde el almacén.
func (d *DB) PutOps(ops []opfmt.Op) (nuevas int, err error) {
	tx, err := d.sql.Begin()
	if err != nil {
		return 0, err
	}
	defer tx.Rollback()
	st, err := tx.Prepare(`INSERT OR IGNORE INTO ops
		(author_id,lamport,author_kind,kind,entity_creator,entity_seq,entity_type,
		 field_id,field_version,wall_clock,value) VALUES (?,?,?,?,?,?,?,?,?,?,?)`)
	if err != nil {
		return 0, err
	}
	defer st.Close()
	for _, op := range ops {
		r, err := st.Exec(int64(op.AuthorID), int64(op.Lamport), op.AuthorKind, op.Kind,
			int64(op.Entity.CreatorID), int64(op.Entity.CreatorSeq), op.EntityType,
			op.FieldID, int64(op.FieldVersion), op.WallClock, op.Value)
		if err != nil {
			return 0, err
		}
		if n, _ := r.RowsAffected(); n > 0 {
			nuevas++
		}
	}
	return nuevas, tx.Commit()
}

// Ops devuelve todas las operaciones en orden determinista.
func (d *DB) Ops() ([]opfmt.Op, error) {
	rows, err := d.sql.Query(`SELECT author_id,lamport,author_kind,kind,entity_creator,
		entity_seq,entity_type,field_id,field_version,wall_clock,value
		FROM ops ORDER BY author_id, lamport`)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var out []opfmt.Op
	for rows.Next() {
		var op opfmt.Op
		var aid, lam, ec, es, fv int64
		if err := rows.Scan(&aid, &lam, &op.AuthorKind, &op.Kind, &ec, &es,
			&op.EntityType, &op.FieldID, &fv, &op.WallClock, &op.Value); err != nil {
			return nil, err
		}
		op.Schema = opfmt.SchemaVersion
		op.AuthorID, op.Lamport = uint64(aid), uint64(lam)
		op.Entity = opfmt.EntityID{CreatorID: uint64(ec), CreatorSeq: uint64(es)}
		op.FieldVersion = uint64(fv)
		out = append(out, op)
	}
	return out, rows.Err()
}

// OpsSince devuelve las operaciones de un autor por encima de una marca de agua.
func (d *DB) OpsSince(authorID, after uint64) ([]opfmt.Op, error) {
	all, err := d.Ops()
	if err != nil {
		return nil, err
	}
	var out []opfmt.Op
	for _, op := range all {
		if op.AuthorID == authorID && op.Lamport > after {
			out = append(out, op)
		}
	}
	return out, nil
}

// PutConflicts guarda las escrituras perdedoras para que la consola las muestre.
func (d *DB) PutConflicts(cs []merge.Conflict) error {
	if len(cs) == 0 {
		return nil
	}
	tx, err := d.sql.Begin()
	if err != nil {
		return err
	}
	defer tx.Rollback()
	now := time.Now().Unix()
	for _, c := range cs {
		if _, err := tx.Exec(`INSERT INTO conflicts (entity_creator,entity_seq,field_id,
			reason,loser_author,loser_kind,loser_value,winner_value,created_at)
			VALUES (?,?,?,?,?,?,?,?,?)`,
			int64(c.Entity.CreatorID), int64(c.Entity.CreatorSeq), c.FieldID, c.Reason,
			int64(c.Loser.AuthorID), c.Loser.AuthorKind, c.Loser.Value, c.Winner.Value, now); err != nil {
			return err
		}
	}
	return tx.Commit()
}

func (d *DB) CountConflicts() (int, error) {
	var n int
	err := d.sql.QueryRow(`SELECT COUNT(*) FROM conflicts`).Scan(&n)
	return n, err
}

// Peer lee y escribe las marcas de agua y el último envelope_seq.
func (d *DB) PeerSeq(peerID uint64) (uint64, error) {
	var v int64
	err := d.sql.QueryRow(`SELECT last_envelope_seq FROM peers WHERE peer_id=?`, int64(peerID)).Scan(&v)
	if err == sql.ErrNoRows {
		return 0, nil
	}
	return uint64(v), err
}

func (d *DB) SetPeerSeq(peerID, seq uint64) error {
	_, err := d.sql.Exec(`INSERT INTO peers (peer_id,last_envelope_seq,last_seen) VALUES (?,?,?)
		ON CONFLICT(peer_id) DO UPDATE SET last_envelope_seq=excluded.last_envelope_seq,
		last_seen=excluded.last_seen`, int64(peerID), int64(seq), time.Now().Unix())
	return err
}

// AppendEvent guarda un evento de la clase C3 tal cual, sin interpretarlo.
func (d *DB) AppendEvent(authorID, lamport, datasetID uint64, schemaVer uint8, payload []byte) (bool, error) {
	r, err := d.sql.Exec(`INSERT OR IGNORE INTO events
		(author_id,lamport,dataset_id,schema,payload,received_at,interpreted)
		VALUES (?,?,?,?,?,?,?)`, int64(authorID), int64(lamport), int64(datasetID),
		schemaVer, payload, time.Now().Unix(), boolToInt(schemaVer <= opfmt.SchemaVersion))
	if err != nil {
		return false, err
	}
	n, _ := r.RowsAffected()
	return n > 0, nil
}

func boolToInt(b bool) int {
	if b {
		return 1
	}
	return 0
}

func (d *DB) CountEvents() (int, error) {
	var n int
	err := d.sql.QueryRow(`SELECT COUNT(*) FROM events`).Scan(&n)
	return n, err
}
