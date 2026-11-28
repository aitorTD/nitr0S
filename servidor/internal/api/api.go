// Package api sirve los extremos HTTPS de las tres clases de sincronización.
//
// Especificado en diseno/registro-de-operaciones-y-sincronizacion.md §1 y §5.
//
// El espacio de rutas hace visible el invariante en mayúsculas del brief: EL
// APARATO SOLO SE PUEDE ACTUALIZAR A TRAVÉS DEL MISMO SERVIDOR, así que el
// saludo y la actualización son un CONTRATO CONGELADO y NO llevan versión en la
// ruta. Todo lo demás vive bajo /v1/ y puede moverse. Un cambio de protocolo
// que rompiera /hello o /update sería irrecuperable sin USB, y este aparato
// puede no tener USB.
package api

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"time"

	"github.com/aitorTD/nitr0S/servidor/internal/merge"
	"github.com/aitorTD/nitr0S/servidor/internal/opfmt"
	"github.com/aitorTD/nitr0S/servidor/internal/store"
	"github.com/aitorTD/nitr0S/servidor/internal/wire"
)

// MaxBody acota el cuerpo de una petición. Un lote de 7 días de desconexión con
// operaciones de 78 bytes no llega ni de lejos.
const MaxBody = 8 << 20

type Config struct {
	ServerID  uint64 // autor de las operaciones del lado del servidor
	DatasetID uint64
	Schema    merge.Schema
	Now       func() time.Time // inyectable para las pruebas
}

type Server struct {
	cfg Config
	db  *store.DB
}

func New(db *store.DB, cfg Config) *Server {
	if cfg.Now == nil {
		cfg.Now = time.Now
	}
	if cfg.Schema == nil {
		cfg.Schema = merge.MapSchema{}
	}
	return &Server{cfg: cfg, db: db}
}

func (s *Server) Routes() *http.ServeMux {
	mux := http.NewServeMux()
	// Contrato congelado. Sin versión, nunca.
	mux.HandleFunc("GET /hello", s.hello)
	mux.HandleFunc("GET /update", s.update)
	// Versionado.
	mux.HandleFunc("POST /v1/sync/ops", s.syncOps)
	mux.HandleFunc("POST /v1/sync/events", s.syncEvents)
	mux.HandleFunc("GET /v1/content/{hash}", s.content)
	return mux
}

// --- El 401 que desbloquea un reloj desviado --------------------------------

type unauthorized struct {
	// El aparato ajusta su reloj con esto y reintenta EXACTAMENTE una vez. Sin
	// este campo, un aparato con el reloj desviado se queda bloqueado para
	// siempre con un mensaje inútil y sin teclado donde arreglarlo.
	ServerTime int64  `json:"server_time"`
	Code       string `json:"code"`
	Message    string `json:"message"`
}

func (s *Server) deny(w http.ResponseWriter, code, msg string) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusUnauthorized)
	_ = json.NewEncoder(w).Encode(unauthorized{
		ServerTime: s.cfg.Now().Unix(), Code: code, Message: msg,
	})
}

func (s *Server) fail(w http.ResponseWriter, status int, code, msg string) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(map[string]any{
		"code": code, "message": msg, "server_time": s.cfg.Now().Unix(),
	})
}

// --- Contrato congelado -----------------------------------------------------

type helloBody struct {
	ServerTime  int64    `json:"server_time"`
	ProtoVer    []uint16 `json:"proto_versions"`
	DatasetID   uint64   `json:"dataset_id"`
	UpdatePath  string   `json:"update_path"`
	FirmwareVer string   `json:"firmware_version,omitempty"`
}

// hello va sin autenticar a propósito: es lo primero que un aparato con el
// reloj perdido necesita, y de aquí saca la hora para que el TLS deje de
// fallar por validez. El HMAC es el autenticador; esto no entrega nada secreto.
func (s *Server) hello(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(helloBody{
		ServerTime: s.cfg.Now().Unix(),
		ProtoVer:   wire.SupportedVersions(),
		DatasetID:  s.cfg.DatasetID,
		UpdatePath: "/update",
	})
}

func (s *Server) update(w http.ResponseWriter, r *http.Request) {
	// Todavía no hay firmware. Se contesta con un cuerpo claro en vez de un 404
	// desnudo, porque este extremo no se puede versionar después.
	s.fail(w, http.StatusNotFound, "UPD-01", "todavía no hay ninguna imagen de firmware publicada")
}

// --- C1: contenido ----------------------------------------------------------

func (s *Server) content(w http.ResponseWriter, r *http.Request) {
	s.fail(w, http.StatusNotFound, "SYS-02",
		fmt.Sprintf("el almacén de contenido direccionado por hash todavía no existe (%s)", r.PathValue("hash")))
}

// --- Autenticación del aparato ----------------------------------------------

// authenticate lee el sobre, busca los secretos vigentes del remitente que el
// propio sobre declara y verifica el HMAC. Un aparato desconocido y una firma
// mala dan el MISMO 401: no se filtra si el aparato existe.
func (s *Server) authenticate(body []byte) (wire.Envelope, []opfmt.Op, error) {
	peek, err := wire.PeekSender(body)
	if err != nil {
		return wire.Envelope{}, nil, err
	}
	secrets, err := s.db.Secrets(peek)
	if err != nil {
		return wire.Envelope{}, nil, err
	}
	if len(secrets) == 0 {
		return wire.Envelope{}, nil, wire.ErrAuth
	}
	return wire.Unmarshal(body, secrets)
}

// accept hace las comprobaciones de sobre con el contador persistido. Si el
// antirreplay viviera en memoria, un reinicio del servidor abriría una ventana
// de replay, que es justo lo que este mecanismo existe para cerrar.
func (s *Server) accept(env wire.Envelope) error {
	if !wire.Supported(env.ProtoVersion) {
		return wire.ErrProtoUnsupported
	}
	if env.DatasetID != s.cfg.DatasetID {
		return wire.ErrDataset
	}
	last, err := s.db.PeerSeq(env.SenderID)
	if err != nil {
		return err
	}
	if env.EnvelopeSeq <= last {
		return wire.ErrReplay
	}
	return s.db.SetPeerSeq(env.SenderID, env.EnvelopeSeq)
}

func (s *Server) readEnvelope(w http.ResponseWriter, r *http.Request) (wire.Envelope, []opfmt.Op, bool) {
	body, err := io.ReadAll(http.MaxBytesReader(w, r.Body, MaxBody))
	if err != nil {
		s.fail(w, http.StatusRequestEntityTooLarge, "SYS-03", "cuerpo demasiado grande")
		return wire.Envelope{}, nil, false
	}
	env, ops, err := s.authenticate(body)
	switch {
	case errors.Is(err, wire.ErrAuth), errors.Is(err, wire.ErrMagic), errors.Is(err, wire.ErrShort):
		s.deny(w, "NET-01", "firma no válida o aparato desconocido")
		return wire.Envelope{}, nil, false
	case err != nil:
		s.fail(w, http.StatusBadRequest, "NET-02", "sobre ilegible")
		return wire.Envelope{}, nil, false
	}
	switch err := s.accept(env); {
	case errors.Is(err, wire.ErrProtoUnsupported):
		// El aparato avisa y sigue funcionando sin red, que es su modo normal.
		s.fail(w, http.StatusConflict, "SYS-04",
			fmt.Sprintf("versión de protocolo %d no soportada; este servidor habla %v",
				env.ProtoVersion, wire.SupportedVersions()))
		return wire.Envelope{}, nil, false
	case errors.Is(err, wire.ErrDataset):
		s.fail(w, http.StatusConflict, "SYS-05",
			"dataset_id distinto: no se fusiona nada")
		return wire.Envelope{}, nil, false
	case errors.Is(err, wire.ErrReplay):
		s.fail(w, http.StatusConflict, "NET-03",
			"envelope_seq no es estrictamente mayor que el último visto")
		return wire.Envelope{}, nil, false
	case err != nil:
		s.fail(w, http.StatusInternalServerError, "SYS-01", "fallo interno")
		return wire.Envelope{}, nil, false
	}
	return env, ops, true
}

// --- C2: fusión del registro de operaciones ---------------------------------

func (s *Server) syncOps(w http.ResponseWriter, r *http.Request) {
	env, ops, ok := s.readEnvelope(w, r)
	if !ok {
		return
	}

	if _, err := s.db.PutOps(ops); err != nil {
		s.fail(w, http.StatusInternalServerError, "STORE-01", "no se pudo guardar el lote")
		return
	}

	// Se refusiona desde el registro completo: el estado es derivado y el
	// registro es la verdad, así que no hay estado incremental que corromper.
	todas, err := s.db.Ops()
	if err != nil {
		s.fail(w, http.StatusInternalServerError, "STORE-02", "no se pudo leer el registro")
		return
	}
	set := merge.New(s.cfg.Schema)
	set.ApplyAll(todas)
	if err := s.db.PutConflicts(set.Conflicts); err != nil {
		s.fail(w, http.StatusInternalServerError, "STORE-03", "no se pudieron guardar los conflictos")
		return
	}

	// Y se le devuelve lo que le falta del flujo del servidor. acked_through es
	// la marca de agua del flujo DEL RECEPTOR que el remitente dice tener: lo
	// contrario no serviría de nada, porque nadie necesita que le cuenten su
	// propio registro.
	pendientes, err := s.db.OpsSince(s.cfg.ServerID, env.AckedThrough)
	if err != nil {
		s.fail(w, http.StatusInternalServerError, "STORE-02", "no se pudo leer el registro")
		return
	}
	secrets, err := s.db.Secrets(env.SenderID)
	if err != nil || len(secrets) == 0 {
		s.fail(w, http.StatusInternalServerError, "SYS-01", "fallo interno")
		return
	}
	var maxLam uint64
	for _, op := range ops {
		if op.Lamport > maxLam {
			maxLam = op.Lamport
		}
	}
	resp := wire.Envelope{
		ProtoVersion: wire.ProtoVersion,
		DatasetID:    s.cfg.DatasetID,
		SenderID:     s.cfg.ServerID,
		SenderKind:   opfmt.AuthorConsole,
		EnvelopeSeq:  env.EnvelopeSeq, // espejo: el aparato correlaciona la respuesta
		AckedThrough: maxLam,          // "de lo tuyo tengo hasta aquí"
	}
	out, err := wire.Marshal(resp, pendientes, secrets[0])
	if err != nil {
		s.fail(w, http.StatusInternalServerError, "SYS-01", "fallo interno")
		return
	}
	w.Header().Set("Content-Type", "application/octet-stream")
	_, _ = w.Write(out)
}

// --- C3: eventos que solo se añaden ----------------------------------------

func (s *Server) syncEvents(w http.ResponseWriter, r *http.Request) {
	env, ops, ok := s.readEnvelope(w, r)
	if !ok {
		return
	}
	nuevos := 0
	for _, op := range ops {
		// Se guardan TAL CUAL, sin interpretarlos: guardar el hecho y derivar
		// después es lo que permite cambiar de planificador sin volver a
		// pedirle nada al aparato.
		buf, err := opfmt.Encode(nil, op)
		if err != nil {
			s.fail(w, http.StatusBadRequest, "NET-02", "evento ilegible")
			return
		}
		n, err := s.db.AppendEvent(op.AuthorID, op.Lamport, env.DatasetID, op.Schema, buf)
		if err != nil {
			s.fail(w, http.StatusInternalServerError, "STORE-04", "no se pudo añadir el evento")
			return
		}
		if n {
			nuevos++
		}
	}
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(map[string]any{
		"accepted": len(ops), "new": nuevos, "server_time": s.cfg.Now().Unix(),
	})
}
