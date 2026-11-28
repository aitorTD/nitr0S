// Package consola es la web de la Pi.
//
// NOTA SOBRE SU ASPECTO, y es deliberada: estas plantillas son HTML semántico
// SIN NINGUNA HOJA DE ESTILO. No es descuido ni provisionalidad perezosa: la
// sección 12.3 del brief fija el orden de la fase visual —primero el inventario
// de pantallas, después dos o tres propuestas de la pantalla de lista del
// aparato, y "la consola se hace después, tomando los mismos tokens"—. Inventar
// aquí una tipografía, unos espaciados o unos colores sería decidir por omisión
// lo que la corrección 8 reserva explícitamente al autor. Así que la consola
// funciona entera y se ve como la ve el navegador, hasta que existan los tokens.
//
// La web es un cliente de la MISMA API, con las mismas reglas y los mismos
// datos: todo lo que escribe pasa por el registro de operaciones.
package consola

import (
	"embed"
	"encoding/hex"
	"errors"
	"fmt"
	"html/template"
	"net/http"
	"strconv"
	"time"

	"github.com/aitorTD/nitr0S/servidor/internal/merge"
	"github.com/aitorTD/nitr0S/servidor/internal/opfmt"
	"github.com/aitorTD/nitr0S/servidor/internal/store"
)

//go:embed plantillas/*.html
var plantillas embed.FS

const (
	cookieNombre = "nitr0s_sesion"
	sesionTTL    = 30 * 24 * time.Hour

	// Tipos de entidad, de la tabla de propiedad del estado.
	TipoItem uint16 = 10
	// Campos del ítem de lista.
	CampoTexto   uint16 = 1
	CampoHecho   uint16 = 2
	CampoLista   uint16 = 3
	campoBorrado        = opfmt.FieldDeleted
)

type Config struct {
	ServerID     uint64
	Schema       merge.Schema
	DeviceID     uint64 // el aparato cuyos estados de entrega se muestran
	Now          func() time.Time
	SecureCookie bool
}

type Consola struct {
	cfg  Config
	db   *store.DB
	tmpl *template.Template
}

func New(db *store.DB, cfg Config) (*Consola, error) {
	if cfg.Now == nil {
		cfg.Now = time.Now
	}
	t, err := template.New("").Funcs(template.FuncMap{
		"fecha": func(ts int64) string {
			if ts == 0 {
				return "nunca"
			}
			return time.Unix(ts, 0).Format("2006-01-02 15:04")
		},
		"texto": func(b []byte) string { return string(b) },
	}).ParseFS(plantillas, "plantillas/*.html")
	if err != nil {
		return nil, err
	}
	return &Consola{cfg: cfg, db: db, tmpl: t}, nil
}

func (c *Consola) Rutas(mux *http.ServeMux) {
	mux.HandleFunc("GET /login", c.loginForm)
	mux.HandleFunc("POST /login", c.login)
	mux.HandleFunc("POST /logout", c.logout)
	mux.HandleFunc("GET /{$}", c.proteger(c.panel))
	mux.HandleFunc("GET /listas", c.proteger(c.listas))
	mux.HandleFunc("POST /listas/crear", c.proteger(c.crearItem))
	mux.HandleFunc("POST /listas/editar", c.proteger(c.editarItem))
	mux.HandleFunc("POST /listas/borrar", c.proteger(c.borrarItem))
	mux.HandleFunc("GET /conflictos", c.proteger(c.conflictos))
	mux.HandleFunc("POST /conflictos/visto", c.proteger(c.conflictoVisto))
	mux.HandleFunc("GET /bancos", c.proteger(c.bancosForm))
	mux.HandleFunc("POST /bancos", c.proteger(c.bancosPublicar))
}

// --- Sesión ------------------------------------------------------------------

func (c *Consola) proteger(h http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		ck, err := r.Cookie(cookieNombre)
		if err != nil {
			http.Redirect(w, r, "/login", http.StatusSeeOther)
			return
		}
		tok, err := hex.DecodeString(ck.Value)
		if err != nil {
			http.Redirect(w, r, "/login", http.StatusSeeOther)
			return
		}
		if _, ok := c.db.Session(tok); !ok {
			http.Redirect(w, r, "/login", http.StatusSeeOther)
			return
		}
		h(w, r)
	}
}

func (c *Consola) loginForm(w http.ResponseWriter, r *http.Request) {
	c.render(w, "login.html", map[string]any{"Error": r.URL.Query().Get("error")})
}

func (c *Consola) login(w http.ResponseWriter, r *http.Request) {
	tok, err := c.db.Login(r.FormValue("usuario"), r.FormValue("clave"), sesionTTL)
	if errors.Is(err, store.ErrBadLogin) {
		http.Redirect(w, r, "/login?error=1", http.StatusSeeOther)
		return
	}
	if err != nil {
		c.fallo(w, err)
		return
	}
	// MaxAge y no Expires: una fecha absoluta se compara contra el reloj del
	// NAVEGADOR, así que un reloj desviado en el portátil tiraría una sesión
	// perfectamente válida. MaxAge es relativo y no depende de ningún reloj.
	http.SetCookie(w, &http.Cookie{
		Name: cookieNombre, Value: hex.EncodeToString(tok), Path: "/",
		HttpOnly: true, Secure: c.cfg.SecureCookie, SameSite: http.SameSiteLaxMode,
		MaxAge: int(sesionTTL.Seconds()),
	})
	http.Redirect(w, r, "/", http.StatusSeeOther)
}

func (c *Consola) logout(w http.ResponseWriter, r *http.Request) {
	if ck, err := r.Cookie(cookieNombre); err == nil {
		if tok, err := hex.DecodeString(ck.Value); err == nil {
			_ = c.db.Logout(tok)
		}
	}
	http.SetCookie(w, &http.Cookie{Name: cookieNombre, Value: "", Path: "/", MaxAge: -1})
	http.Redirect(w, r, "/login", http.StatusSeeOther)
}

// --- Panel -------------------------------------------------------------------

func (c *Consola) panel(w http.ResponseWriter, r *http.Request) {
	ops, err := c.db.Ops()
	if err != nil {
		c.fallo(w, err)
		return
	}
	conf, _ := c.db.Conflicts(1000)
	ev, _ := c.db.CountEvents()
	acked, _ := c.db.PeerAcked(c.cfg.DeviceID)
	visto, _ := c.db.PeerLastSeen(c.cfg.DeviceID)

	var pendientes int
	for _, op := range ops {
		if op.AuthorID == c.cfg.ServerID && op.Lamport > acked {
			pendientes++
		}
	}
	c.render(w, "panel.html", map[string]any{
		"Operaciones":  len(ops),
		"Conflictos":   len(conf),
		"Eventos":      ev,
		"Pendientes":   pendientes,
		"AckedThrough": acked,
		"UltimaSync":   visto,
		"DeviceID":     c.cfg.DeviceID,
	})
}

// --- Listas ------------------------------------------------------------------

type fila struct {
	ID        opfmt.EntityID
	Clave     string
	Texto     string
	Hecho     bool
	Borrado   bool
	Entregado bool // los dos estados de entrega que el brief exige mostrar
}

func (c *Consola) estado(r *http.Request) (*merge.Set, uint64, error) {
	ops, err := c.db.Ops()
	if err != nil {
		return nil, 0, err
	}
	set := merge.New(c.cfg.Schema)
	set.ApplyAll(ops)
	acked, err := c.db.PeerAcked(c.cfg.DeviceID)
	return set, acked, err
}

func (c *Consola) listas(w http.ResponseWriter, r *http.Request) {
	set, acked, err := c.estado(r)
	if err != nil {
		c.fallo(w, err)
		return
	}
	var filas []fila
	for _, e := range set.Entities() {
		if e.Type != TipoItem {
			continue
		}
		f := fila{ID: e.ID, Clave: clave(e.ID), Borrado: e.Deleted(), Entregado: true}
		if v, ok := e.Field(CampoTexto); ok {
			f.Texto = string(v)
		}
		if v, ok := e.Field(CampoHecho); ok && len(v) > 0 && v[0] == 1 {
			f.Hecho = true
		}
		// Entregado = toda operación del servidor sobre esta entidad está por
		// debajo de la marca de agua que el aparato confirmó.
		for _, op := range e.Fields {
			if op.AuthorID == c.cfg.ServerID && op.Lamport > acked {
				f.Entregado = false
			}
		}
		if e.CreateOp != nil && e.CreateOp.AuthorID == c.cfg.ServerID && e.CreateOp.Lamport > acked {
			f.Entregado = false
		}
		filas = append(filas, f)
	}
	c.render(w, "listas.html", map[string]any{
		"Filas": filas, "Error": r.URL.Query().Get("error"),
	})
}

func clave(e opfmt.EntityID) string {
	return fmt.Sprintf("%d-%d", e.CreatorID, e.CreatorSeq)
}

func parseClave(s string) (opfmt.EntityID, error) {
	var a, b uint64
	if _, err := fmt.Sscanf(s, "%d-%d", &a, &b); err != nil {
		return opfmt.EntityID{}, err
	}
	return opfmt.EntityID{CreatorID: a, CreatorSeq: b}, nil
}

// emitir escribe una operación de la consola en el registro. Es el único camino
// de escritura: la web no toca el estado, añade al registro como todo el mundo.
func (c *Consola) emitir(kind opfmt.OpKind, e opfmt.EntityID, tipo uint16,
	field uint16, valor []byte) error {
	lam, err := c.db.NextLamport(c.cfg.ServerID)
	if err != nil {
		return err
	}
	op := opfmt.Op{
		Schema: opfmt.SchemaVersion, Kind: kind,
		AuthorID: c.cfg.ServerID, AuthorKind: opfmt.AuthorConsole,
		Lamport: lam, WallClock: c.cfg.Now().Unix(),
		EntityType: tipo, Entity: e, FieldID: field, Value: valor,
	}
	if kind != opfmt.OpCreate {
		f := field
		if kind == opfmt.OpDelete {
			f = campoBorrado
		}
		ver, err := c.db.NextFieldVersion(e, f)
		if err != nil {
			return err
		}
		op.FieldVersion = ver
	}
	_, err = c.db.PutOps([]opfmt.Op{op})
	return err
}

func (c *Consola) crearItem(w http.ResponseWriter, r *http.Request) {
	texto := r.FormValue("texto")
	if texto == "" {
		http.Redirect(w, r, "/listas?error=el+texto+no+puede+estar+vac%C3%ADo", http.StatusSeeOther)
		return
	}
	seq, err := c.db.NextCreatorSeq(c.cfg.ServerID)
	if err != nil {
		c.fallo(w, err)
		return
	}
	e := opfmt.EntityID{CreatorID: c.cfg.ServerID, CreatorSeq: seq}
	if err := c.emitir(opfmt.OpCreate, e, TipoItem, 0, nil); err != nil {
		c.fallo(w, err)
		return
	}
	if err := c.emitir(opfmt.OpSetField, e, TipoItem, CampoTexto, []byte(texto)); err != nil {
		c.fallo(w, err)
		return
	}
	http.Redirect(w, r, "/listas", http.StatusSeeOther)
}

func (c *Consola) editarItem(w http.ResponseWriter, r *http.Request) {
	e, err := parseClave(r.FormValue("id"))
	if err != nil {
		http.Error(w, "identificador no válido", http.StatusBadRequest)
		return
	}
	if err := c.emitir(opfmt.OpSetField, e, TipoItem, CampoTexto, []byte(r.FormValue("texto"))); err != nil {
		c.fallo(w, err)
		return
	}
	http.Redirect(w, r, "/listas", http.StatusSeeOther)
}

func (c *Consola) borrarItem(w http.ResponseWriter, r *http.Request) {
	e, err := parseClave(r.FormValue("id"))
	if err != nil {
		http.Error(w, "identificador no válido", http.StatusBadRequest)
		return
	}
	// Un borrado es un valor, no una ausencia.
	if err := c.emitir(opfmt.OpDelete, e, TipoItem, campoBorrado, []byte{1}); err != nil {
		c.fallo(w, err)
		return
	}
	http.Redirect(w, r, "/listas", http.StatusSeeOther)
}

// --- Conflictos ---------------------------------------------------------------

func (c *Consola) conflictos(w http.ResponseWriter, r *http.Request) {
	cs, err := c.db.Conflicts(200)
	if err != nil {
		c.fallo(w, err)
		return
	}
	c.render(w, "conflictos.html", map[string]any{"Conflictos": cs})
}

func (c *Consola) conflictoVisto(w http.ResponseWriter, r *http.Request) {
	id, err := strconv.ParseInt(r.FormValue("id"), 10, 64)
	if err != nil {
		http.Error(w, "identificador no válido", http.StatusBadRequest)
		return
	}
	if err := c.db.AckConflict(id); err != nil {
		c.fallo(w, err)
		return
	}
	http.Redirect(w, r, "/conflictos", http.StatusSeeOther)
}

// --- Bancos -------------------------------------------------------------------

func (c *Consola) bancosForm(w http.ResponseWriter, r *http.Request) {
	c.render(w, "bancos.html", map[string]any{})
}

func (c *Consola) bancosPublicar(w http.ResponseWriter, r *http.Request) {
	crudo := []byte(r.FormValue("json"))
	b, errs := Validar(crudo, nil)
	if errs != nil {
		c.render(w, "bancos.html", map[string]any{"Errores": errs, "JSON": string(crudo)})
		return
	}
	c.render(w, "bancos.html", map[string]any{
		"Ok": fmt.Sprintf("banco %d (%s) válido: %d preguntas. La publicación al canal de contenido todavía no existe.",
			b.BankID, b.Nombre, len(b.Preguntas)),
	})
}

// --- Utilidades ---------------------------------------------------------------

func (c *Consola) render(w http.ResponseWriter, nombre string, datos map[string]any) {
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	if err := c.tmpl.ExecuteTemplate(w, nombre, datos); err != nil {
		http.Error(w, "no se pudo pintar la página", http.StatusInternalServerError)
	}
}

func (c *Consola) fallo(w http.ResponseWriter, err error) {
	http.Error(w, "SYS-01: "+err.Error(), http.StatusInternalServerError)
}
