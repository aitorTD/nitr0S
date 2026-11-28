package consola_test

import (
	"net/http"
	"net/http/cookiejar"
	"net/http/httptest"
	"net/url"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
	"time"

	"github.com/aitorTD/nitr0S/servidor/internal/consola"
	"github.com/aitorTD/nitr0S/servidor/internal/merge"
	"github.com/aitorTD/nitr0S/servidor/internal/opfmt"
	"github.com/aitorTD/nitr0S/servidor/internal/store"
)

const (
	serverID uint64 = 1
	devID    uint64 = 0xD0
	clave           = "contraseña-larga"
)

type fix struct {
	t   *testing.T
	db  *store.DB
	srv *httptest.Server
	cl  *http.Client
}

func nuevo(t *testing.T) *fix {
	t.Helper()
	db, err := store.Open(filepath.Join(t.TempDir(), "c.db"))
	if err != nil {
		t.Fatal(err)
	}
	if err := db.AddUser("aitor", clave); err != nil {
		t.Fatal(err)
	}
	c, err := consola.New(db, consola.Config{
		ServerID: serverID, DeviceID: devID,
		Schema: merge.MapSchema{consola.TipoItem: merge.OwnerShared},
		Now:    func() time.Time { return time.Unix(1764300000, 0) },
	})
	if err != nil {
		t.Fatal(err)
	}
	mux := http.NewServeMux()
	c.Rutas(mux)
	srv := httptest.NewServer(mux)
	jar, _ := cookiejar.New(nil)
	t.Cleanup(func() { srv.Close(); db.Close() })
	return &fix{t: t, db: db, srv: srv, cl: &http.Client{Jar: jar}}
}

func (f *fix) get(ruta string) (*http.Response, string) {
	f.t.Helper()
	resp, err := f.cl.Get(f.srv.URL + ruta)
	if err != nil {
		f.t.Fatal(err)
	}
	defer resp.Body.Close()
	b := make([]byte, 1<<20)
	n, _ := resp.Body.Read(b)
	return resp, string(b[:n])
}

func (f *fix) post(ruta string, vals url.Values) (*http.Response, string) {
	f.t.Helper()
	resp, err := f.cl.PostForm(f.srv.URL+ruta, vals)
	if err != nil {
		f.t.Fatal(err)
	}
	defer resp.Body.Close()
	b := make([]byte, 1<<20)
	n, _ := resp.Body.Read(b)
	return resp, string(b[:n])
}

func (f *fix) entrar() {
	f.t.Helper()
	resp, _ := f.post("/login", url.Values{"usuario": {"aitor"}, "clave": {clave}})
	if resp.Request.URL.Path != "/" {
		f.t.Fatalf("tras entrar debería estar en /, está en %s", resp.Request.URL.Path)
	}
}

// --- Sesión -------------------------------------------------------------------

func TestSinSesionTodoRedirigeALogin(t *testing.T) {
	f := nuevo(t)
	// Sin seguir redirecciones, para ver el 303.
	f.cl.CheckRedirect = func(*http.Request, []*http.Request) error { return http.ErrUseLastResponse }
	for _, ruta := range []string{"/", "/listas", "/conflictos", "/bancos"} {
		resp, _ := f.get(ruta)
		if resp.StatusCode != http.StatusSeeOther {
			t.Fatalf("%s sin sesión debería redirigir, salió %d", ruta, resp.StatusCode)
		}
		if loc := resp.Header.Get("Location"); loc != "/login" {
			t.Fatalf("%s debería ir a /login, va a %s", ruta, loc)
		}
	}
}

func TestClaveMalaNoAbreSesion(t *testing.T) {
	f := nuevo(t)
	resp, cuerpo := f.post("/login", url.Values{"usuario": {"aitor"}, "clave": {"equivocada"}})
	if resp.Request.URL.Path != "/login" {
		t.Fatalf("debería seguir en /login, está en %s", resp.Request.URL.Path)
	}
	if !strings.Contains(cuerpo, "incorrectos") {
		t.Fatal("debería decir que son incorrectos")
	}
}

func TestSalirCierraLaSesion(t *testing.T) {
	f := nuevo(t)
	f.entrar()
	if _, c := f.get("/"); !strings.Contains(c, "Panel") {
		t.Fatal("debería ver el panel")
	}
	f.post("/logout", nil)
	f.cl.CheckRedirect = func(*http.Request, []*http.Request) error { return http.ErrUseLastResponse }
	resp, _ := f.get("/")
	if resp.StatusCode != http.StatusSeeOther {
		t.Fatalf("tras salir debería redirigir, salió %d", resp.StatusCode)
	}
}

// --- Escribir pasa por el registro de operaciones -----------------------------

func TestCrearEditarYBorrarEmitenOperaciones(t *testing.T) {
	f := nuevo(t)
	f.entrar()

	f.post("/listas/crear", url.Values{"texto": {"leche"}})
	ops, err := f.db.Ops()
	if err != nil {
		t.Fatal(err)
	}
	if len(ops) != 2 { // create + set_field(texto)
		t.Fatalf("crear debe emitir 2 operaciones, emitió %d", len(ops))
	}
	for _, op := range ops {
		if op.AuthorKind != opfmt.AuthorConsole || op.AuthorID != serverID {
			t.Fatal("las escrituras de la consola deben ir firmadas como de la consola")
		}
	}

	_, cuerpo := f.get("/listas")
	if !strings.Contains(cuerpo, "leche") {
		t.Fatal("el ítem debería aparecer en la lista")
	}

	// La clave de la entidad es (creator_id, creator_seq).
	id := "1-1"
	f.post("/listas/editar", url.Values{"id": {id}, "texto": {"leche entera"}})
	if _, c := f.get("/listas"); !strings.Contains(c, "leche entera") {
		t.Fatal("la edición debería verse")
	}

	f.post("/listas/borrar", url.Values{"id": {id}})
	_, c := f.get("/listas")
	if !strings.Contains(c, "<del>") {
		t.Fatal("un borrado debería mostrarse tachado, no desaparecer: es un valor, no una ausencia")
	}

	// Y el borrado es una operación más, no una fila que se va.
	ops, _ = f.db.Ops()
	var borrados int
	for _, op := range ops {
		if op.Kind == opfmt.OpDelete {
			borrados++
		}
	}
	if borrados != 1 {
		t.Fatalf("debería haber 1 operación de borrado, hay %d", borrados)
	}
}

// Las versiones de campo tienen que subir solas, o la segunda edición no gana.
func TestLasEdicionesSucesivasSubenLaVersionDeCampo(t *testing.T) {
	f := nuevo(t)
	f.entrar()
	f.post("/listas/crear", url.Values{"texto": {"uno"}})
	for _, v := range []string{"dos", "tres", "cuatro"} {
		f.post("/listas/editar", url.Values{"id": {"1-1"}, "texto": {v}})
	}
	_, c := f.get("/listas")
	if !strings.Contains(c, "cuatro") {
		t.Fatal("debe quedar la última edición")
	}
	ops, _ := f.db.Ops()
	var maxVer uint64
	for _, op := range ops {
		if op.FieldID == consola.CampoTexto && op.FieldVersion > maxVer {
			maxVer = op.FieldVersion
		}
	}
	if maxVer != 4 {
		t.Fatalf("cuatro escrituras del campo texto deben llegar a la versión 4, llegó a %d", maxVer)
	}
}

// --- Los dos estados de entrega ----------------------------------------------

func TestEstadosDeEntrega(t *testing.T) {
	f := nuevo(t)
	f.entrar()
	f.post("/listas/crear", url.Values{"texto": {"pan"}})

	_, c := f.get("/listas")
	if !strings.Contains(c, "<strong>no</strong>") {
		t.Fatal("recién escrito, el aparato no lo tiene: debe salir como no entregado")
	}
	_, p := f.get("/")
	if !strings.Contains(p, "2 operaciones") {
		t.Fatalf("el panel debe decir cuántas están guardadas sin entregar:\n%s", p)
	}

	// El aparato confirma hasta el lamport 2.
	if err := f.db.SetPeerAcked(devID, 2); err != nil {
		t.Fatal(err)
	}
	_, c2 := f.get("/listas")
	if strings.Contains(c2, "<strong>no</strong>") {
		t.Fatal("confirmado por el aparato, debe salir como entregado")
	}
	_, p2 := f.get("/")
	if !strings.Contains(p2, "0 operaciones") {
		t.Fatalf("ya no debe quedar nada sin entregar:\n%s", p2)
	}
}

// --- Conflictos ---------------------------------------------------------------

func TestLosConflictosSeVenYSePuedenMarcar(t *testing.T) {
	f := nuevo(t)
	f.entrar()

	// Un empate que el aparato gana, con la edición de la consola perdiendo.
	e := opfmt.EntityID{CreatorID: serverID, CreatorSeq: 1}
	mk := func(a uint64, ak opfmt.AuthorKind, lam, ver uint64, v string) opfmt.Op {
		return opfmt.Op{Schema: opfmt.SchemaVersion, Kind: opfmt.OpSetField, AuthorID: a,
			AuthorKind: ak, Lamport: lam, EntityType: consola.TipoItem, Entity: e,
			FieldID: consola.CampoTexto, FieldVersion: ver, Value: []byte(v)}
	}
	ops := []opfmt.Op{
		mk(serverID, opfmt.AuthorConsole, 1, 7, "leche entera"),
		mk(devID, opfmt.AuthorDevice, 1, 7, "leche"),
	}
	if _, err := f.db.PutOps(ops); err != nil {
		t.Fatal(err)
	}
	set := merge.New(merge.MapSchema{consola.TipoItem: merge.OwnerShared})
	set.ApplyAll(ops)
	if err := f.db.PutConflicts(set.Conflicts); err != nil {
		t.Fatal(err)
	}

	_, c := f.get("/conflictos")
	if !strings.Contains(c, "leche entera") || !strings.Contains(c, "leche") {
		t.Fatal("la consola debe mostrar lo que se intentó escribir y lo que quedó")
	}
	if !strings.Contains(c, "gana el lado dueño") {
		t.Fatalf("debe explicar por qué se perdió:\n%s", c)
	}

	cs, _ := f.db.Conflicts(10)
	f.post("/conflictos/visto", url.Values{"id": {strconv.FormatInt(cs[0].ID, 10)}})
	if _, c2 := f.get("/conflictos"); !strings.Contains(c2, "No hay ningún conflicto") {
		t.Fatal("marcado como visto debería desaparecer de la lista")
	}
}
