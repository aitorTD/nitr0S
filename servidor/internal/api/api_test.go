package api_test

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"path/filepath"
	"testing"
	"time"

	"github.com/aitorTD/nitr0S/servidor/internal/api"
	"github.com/aitorTD/nitr0S/servidor/internal/merge"
	"github.com/aitorTD/nitr0S/servidor/internal/opfmt"
	"github.com/aitorTD/nitr0S/servidor/internal/store"
	"github.com/aitorTD/nitr0S/servidor/internal/wire"
)

const (
	devID     uint64 = 0xD0
	serverID  uint64 = 0x01
	datasetID uint64 = 2001
	typeItem  uint16 = 10
	fText     uint16 = 1
)

var secreto = []byte("secreto-del-aparato")

type fixture struct {
	t    *testing.T
	db   *store.DB
	srv  *httptest.Server
	path string
}

func nuevo(t *testing.T) *fixture {
	t.Helper()
	path := filepath.Join(t.TempDir(), "nitr0s.db")
	return abrirEn(t, path)
}

func abrirEn(t *testing.T, path string) *fixture {
	t.Helper()
	db, err := store.Open(path)
	if err != nil {
		t.Fatal(err)
	}
	if err := db.AddSecret(devID, secreto); err != nil {
		t.Fatal(err)
	}
	h := api.New(db, api.Config{
		ServerID: serverID, DatasetID: datasetID,
		Schema: merge.MapSchema{typeItem: merge.OwnerShared},
		Now:    func() time.Time { return time.Unix(1764300000, 0) },
	})
	srv := httptest.NewServer(h.Routes())
	t.Cleanup(func() { srv.Close(); db.Close() })
	return &fixture{t: t, db: db, srv: srv, path: path}
}

func op(author uint64, ak opfmt.AuthorKind, lam uint64, creator, seq uint64, ver uint64, v string) opfmt.Op {
	return opfmt.Op{Schema: opfmt.SchemaVersion, Kind: opfmt.OpSetField, AuthorID: author,
		AuthorKind: ak, Lamport: lam, EntityType: typeItem,
		Entity:  opfmt.EntityID{CreatorID: creator, CreatorSeq: seq},
		FieldID: fText, FieldVersion: ver, Value: []byte(v)}
}

// post manda un sobre firmado y devuelve la respuesta cruda.
func (f *fixture) post(ruta string, env wire.Envelope, ops []opfmt.Op, secret []byte) *http.Response {
	f.t.Helper()
	body, err := wire.Marshal(env, ops, secret)
	if err != nil {
		f.t.Fatal(err)
	}
	resp, err := http.Post(f.srv.URL+ruta, "application/octet-stream", bytesReader(body))
	if err != nil {
		f.t.Fatal(err)
	}
	return resp
}

func sobre(seq uint64, acked uint64) wire.Envelope {
	return wire.Envelope{ProtoVersion: wire.ProtoVersion, DatasetID: datasetID,
		SenderID: devID, SenderKind: opfmt.AuthorDevice, EnvelopeSeq: seq, AckedThrough: acked}
}

// --- El contrato congelado ---------------------------------------------------

func TestHelloSinAutenticarYSinVersionEnLaRuta(t *testing.T) {
	f := nuevo(t)
	resp, err := http.Get(f.srv.URL + "/hello")
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != 200 {
		t.Fatalf("/hello debe contestar sin autenticar, salió %d", resp.StatusCode)
	}
	var b struct {
		ServerTime    int64    `json:"server_time"`
		ProtoVersions []uint16 `json:"proto_versions"`
		DatasetID     uint64   `json:"dataset_id"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&b); err != nil {
		t.Fatal(err)
	}
	if b.ServerTime == 0 {
		t.Fatal("/hello debe dar la hora: es de donde un aparato con el reloj perdido la saca")
	}
	if len(b.ProtoVersions) == 0 || b.DatasetID != datasetID {
		t.Fatalf("respuesta incompleta: %+v", b)
	}
	// Y no existe bajo /v1: el contrato congelado no lleva versión en la ruta.
	r2, err := http.Get(f.srv.URL + "/v1/hello")
	if err != nil {
		t.Fatal(err)
	}
	defer r2.Body.Close()
	if r2.StatusCode != 404 {
		t.Fatalf("/v1/hello no debe existir: el saludo no se versiona, salió %d", r2.StatusCode)
	}
}

func TestUpdateExisteYExplicaQueNoHayImagen(t *testing.T) {
	f := nuevo(t)
	resp, err := http.Get(f.srv.URL + "/update")
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != 404 {
		t.Fatalf("todavía no hay firmware, salió %d", resp.StatusCode)
	}
	var b map[string]any
	if err := json.NewDecoder(resp.Body).Decode(&b); err != nil {
		t.Fatal(err)
	}
	if b["code"] != "UPD-01" {
		t.Fatalf("un extremo que no se puede versionar después debe contestar con un cuerpo claro: %+v", b)
	}
}

// --- C2 -----------------------------------------------------------------------

func TestSyncOpsCaminoFeliz(t *testing.T) {
	f := nuevo(t)
	ops := []opfmt.Op{
		op(devID, opfmt.AuthorDevice, 1, devID, 1, 1, "leche"),
		op(devID, opfmt.AuthorDevice, 2, devID, 2, 1, "pan"),
	}
	resp := f.post("/v1/sync/ops", sobre(1, 0), ops, secreto)
	defer resp.Body.Close()
	if resp.StatusCode != 200 {
		t.Fatalf("salió %d", resp.StatusCode)
	}
	guardadas, err := f.db.Ops()
	if err != nil {
		t.Fatal(err)
	}
	if len(guardadas) != 2 {
		t.Fatalf("se esperaban 2 operaciones guardadas, hay %d", len(guardadas))
	}
}

// Una sola ronda por HTTP y los dos lados convergen: es la matriz de conflictos
// atravesando el transporte de verdad.
func TestConvergenciaEnUnaRondaPorHttp(t *testing.T) {
	f := nuevo(t)

	// El servidor ya tiene lo suyo: la consola escribió mientras no había red.
	var delServidor []opfmt.Op
	for i := uint64(1); i <= 3; i++ {
		lam, err := f.db.NextLamport(serverID)
		if err != nil {
			t.Fatal(err)
		}
		delServidor = append(delServidor, op(serverID, opfmt.AuthorConsole, lam, serverID, i, 1, "de la consola"))
	}
	if _, err := f.db.PutOps(delServidor); err != nil {
		t.Fatal(err)
	}

	// El aparato sube las suyas, diciendo que del servidor no tiene nada.
	delAparato := []opfmt.Op{
		op(devID, opfmt.AuthorDevice, 1, devID, 1, 1, "del aparato"),
		op(devID, opfmt.AuthorDevice, 2, devID, 2, 1, "del aparato"),
	}
	resp := f.post("/v1/sync/ops", sobre(1, 0), delAparato, secreto)
	defer resp.Body.Close()
	if resp.StatusCode != 200 {
		t.Fatalf("salió %d", resp.StatusCode)
	}
	cuerpo := leerTodo(t, resp.Body)
	_, recibidas, err := wire.Unmarshal(cuerpo, [][]byte{secreto})
	if err != nil {
		t.Fatalf("la respuesta debe venir firmada y legible: %v", err)
	}
	if len(recibidas) != 3 {
		t.Fatalf("el servidor debe devolver sus 3 operaciones, devolvió %d", len(recibidas))
	}

	schema := merge.MapSchema{typeItem: merge.OwnerShared}
	enElAparato := merge.New(schema)
	enElAparato.ApplyAll(delAparato)
	enElAparato.ApplyAll(recibidas)

	todas, err := f.db.Ops()
	if err != nil {
		t.Fatal(err)
	}
	enElServidor := merge.New(schema)
	enElServidor.ApplyAll(todas)

	if !merge.Equal(enElAparato, enElServidor) {
		t.Fatal("una sola ronda por HTTP debe hacer converger los dos lados")
	}
	if n := len(enElServidor.Entities()); n != 5 {
		t.Fatalf("cero pérdidas y cero duplicados: se esperaban 5 entidades, hay %d", n)
	}
}

// La marca de agua evita reenviar lo que el aparato ya tiene.
func TestMarcaDeAguaNoReenviaLoYaVisto(t *testing.T) {
	f := nuevo(t)
	var delServidor []opfmt.Op
	for i := uint64(1); i <= 4; i++ {
		lam, _ := f.db.NextLamport(serverID)
		delServidor = append(delServidor, op(serverID, opfmt.AuthorConsole, lam, serverID, i, 1, "x"))
	}
	if _, err := f.db.PutOps(delServidor); err != nil {
		t.Fatal(err)
	}
	// El aparato dice tener hasta el lamport 2 del servidor.
	resp := f.post("/v1/sync/ops", sobre(1, 2), nil, secreto)
	defer resp.Body.Close()
	_, recibidas, err := wire.Unmarshal(leerTodo(t, resp.Body), [][]byte{secreto})
	if err != nil {
		t.Fatal(err)
	}
	if len(recibidas) != 2 {
		t.Fatalf("solo deben venir las 2 que le faltan, vinieron %d", len(recibidas))
	}
	for _, op := range recibidas {
		if op.Lamport <= 2 {
			t.Fatalf("no debe reenviar el lamport %d", op.Lamport)
		}
	}
}

func TestIdempotenciaPorHttp(t *testing.T) {
	f := nuevo(t)
	ops := []opfmt.Op{op(devID, opfmt.AuthorDevice, 1, devID, 1, 1, "leche")}
	for seq := uint64(1); seq <= 4; seq++ {
		// Mismo lote, envelope_seq nuevo: es exactamente lo que hace un reintento.
		resp := f.post("/v1/sync/ops", sobre(seq, 0), ops, secreto)
		if resp.StatusCode != 200 {
			t.Fatalf("seq %d: salió %d", seq, resp.StatusCode)
		}
		resp.Body.Close()
	}
	guardadas, _ := f.db.Ops()
	if len(guardadas) != 1 {
		t.Fatalf("cuatro reintentos del mismo lote: debe haber 1 operación, hay %d", len(guardadas))
	}
}

// --- Rechazos ----------------------------------------------------------------

func TestFirmaMalaDa401ConLaHoraDelServidor(t *testing.T) {
	f := nuevo(t)
	resp := f.post("/v1/sync/ops", sobre(1, 0), nil, []byte("secreto-equivocado"))
	defer resp.Body.Close()
	if resp.StatusCode != 401 {
		t.Fatalf("salió %d", resp.StatusCode)
	}
	var b struct {
		ServerTime int64  `json:"server_time"`
		Code       string `json:"code"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&b); err != nil {
		t.Fatal(err)
	}
	if b.ServerTime == 0 {
		t.Fatal("el 401 DEBE llevar la hora del servidor: es el camino de recuperación por desvío de reloj")
	}
}

func TestAparatoDesconocidoDaElMismo401(t *testing.T) {
	f := nuevo(t)
	env := sobre(1, 0)
	env.SenderID = 0xBEEF // no existe
	resp := f.post("/v1/sync/ops", env, nil, secreto)
	defer resp.Body.Close()
	if resp.StatusCode != 401 {
		t.Fatalf("un aparato desconocido y una firma mala deben dar el mismo 401, salió %d", resp.StatusCode)
	}
}

func TestRotacionDeClaves(t *testing.T) {
	f := nuevo(t)
	nuevoSecreto := []byte("secreto-nuevo")
	if err := f.db.AddSecret(devID, nuevoSecreto); err != nil {
		t.Fatal(err)
	}
	// Durante la ventana de solape los dos valen.
	for i, s := range [][]byte{secreto, nuevoSecreto} {
		resp := f.post("/v1/sync/ops", sobre(uint64(i+1), 0), nil, s)
		if resp.StatusCode != 200 {
			t.Fatalf("secreto %d debería valer, salió %d", i, resp.StatusCode)
		}
		resp.Body.Close()
	}
	// Revocado el viejo, deja de valer.
	if err := f.db.RevokeSecret(devID, secreto); err != nil {
		t.Fatal(err)
	}
	resp := f.post("/v1/sync/ops", sobre(3, 0), nil, secreto)
	defer resp.Body.Close()
	if resp.StatusCode != 401 {
		t.Fatalf("un secreto revocado no debe valer, salió %d", resp.StatusCode)
	}
}

func TestProtocoloYDatasetRechazados(t *testing.T) {
	f := nuevo(t)
	env := sobre(1, 0)
	env.ProtoVersion = 99
	resp := f.post("/v1/sync/ops", env, nil, secreto)
	if resp.StatusCode != 409 {
		t.Fatalf("protocolo no soportado: salió %d", resp.StatusCode)
	}
	resp.Body.Close()

	env2 := sobre(2, 0)
	env2.DatasetID = 9999
	resp2 := f.post("/v1/sync/ops", env2, nil, secreto)
	defer resp2.Body.Close()
	if resp2.StatusCode != 409 {
		t.Fatalf("dataset distinto: salió %d", resp2.StatusCode)
	}
	if n, _ := f.db.Ops(); len(n) != 0 {
		t.Fatal("un lote rechazado no fusiona nada")
	}
}

// El antirreplay tiene que sobrevivir a un reinicio del servidor: si viviera en
// memoria, reiniciar abriría exactamente la ventana que cierra.
func TestAntirreplayPersisteTrasReiniciar(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "nitr0s.db")

	f := abrirEn(t, path)
	resp := f.post("/v1/sync/ops", sobre(5, 0), nil, secreto)
	if resp.StatusCode != 200 {
		t.Fatalf("salió %d", resp.StatusCode)
	}
	resp.Body.Close()
	f.srv.Close()
	f.db.Close()

	g := abrirEn(t, path) // "reinicio"
	for _, seq := range []uint64{5, 4, 1} {
		resp := g.post("/v1/sync/ops", sobre(seq, 0), nil, secreto)
		if resp.StatusCode != 409 {
			t.Fatalf("seq %d tras reiniciar debe seguir siendo replay, salió %d", seq, resp.StatusCode)
		}
		resp.Body.Close()
	}
	resp = g.post("/v1/sync/ops", sobre(6, 0), nil, secreto)
	defer resp.Body.Close()
	if resp.StatusCode != 200 {
		t.Fatalf("seq 6 sí debe aceptarse, salió %d", resp.StatusCode)
	}
}

// --- C3 -----------------------------------------------------------------------

func TestEventosSoloAnadirPorHttp(t *testing.T) {
	f := nuevo(t)
	evs := []opfmt.Op{
		op(devID, opfmt.AuthorDevice, 1, devID, 1, 1, "repaso 1"),
		op(devID, opfmt.AuthorDevice, 2, devID, 1, 1, "repaso 2"),
	}
	resp := f.post("/v1/sync/events", sobre(1, 0), evs, secreto)
	defer resp.Body.Close()
	if resp.StatusCode != 200 {
		t.Fatalf("salió %d", resp.StatusCode)
	}
	var b struct{ Accepted, New int }
	if err := json.NewDecoder(resp.Body).Decode(&b); err != nil {
		t.Fatal(err)
	}
	if b.Accepted != 2 || b.New != 2 {
		t.Fatalf("salió %+v", b)
	}
	// Reenvío: aceptados pero ninguno nuevo.
	r2 := f.post("/v1/sync/events", sobre(2, 0), evs, secreto)
	defer r2.Body.Close()
	var b2 struct{ Accepted, New int }
	if err := json.NewDecoder(r2.Body).Decode(&b2); err != nil {
		t.Fatal(err)
	}
	if b2.Accepted != 2 || b2.New != 0 {
		t.Fatalf("un reenvío no añade eventos: %+v", b2)
	}
	if n, _ := f.db.CountEvents(); n != 2 {
		t.Fatalf("debe haber 2 eventos, hay %d", n)
	}
}
