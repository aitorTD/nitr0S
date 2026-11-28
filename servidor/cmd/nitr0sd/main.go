// nitr0sd es el servidor de nitr0S: un binario único con SQLite dentro.
//
// Sirve las tres clases de sincronización de
// diseno/registro-de-operaciones-y-sincronizacion.md. Escucha en claro: el TLS
// lo pone Tailscale Funnel por delante, y por eso el autenticador es el HMAC y
// no el TLS.
package main

import (
	"bufio"
	"context"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"runtime"
	"strings"
	"syscall"
	"time"

	"github.com/aitorTD/nitr0S/servidor/internal/api"
	"github.com/aitorTD/nitr0S/servidor/internal/consola"
	"github.com/aitorTD/nitr0S/servidor/internal/merge"
	"github.com/aitorTD/nitr0S/servidor/internal/store"
	"github.com/aitorTD/nitr0S/servidor/internal/wire"
)

// Tipos de entidad de v1 y su propiedad, según la tabla del documento §6.
// Se declara aquí hasta que el registro de esquemas lo ingiera del manifiesto.
var esquemaV1 = merge.MapSchema{
	10: merge.OwnerShared, // ítem de lista: "hecho" quiere autoridad del aparato
	11: merge.OwnerShared, // lista: "se llama así" quiere autoridad de la consola
	20: merge.OwnerServer, // banco de preguntas
	30: merge.OwnerDevice, // ajustes del aparato
}

func main() {
	var (
		db        = flag.String("db", "nitr0s.db", "ruta del fichero SQLite")
		listen    = flag.String("listen", "127.0.0.1:8080", "dirección donde escuchar")
		serverID  = flag.Uint64("server-id", 1, "author_id del lado del servidor")
		datasetID = flag.Uint64("dataset", 2001, "dataset_id que este servidor acepta")
		provision = flag.Uint64("provision", 0, "da de alta un aparato con un secreto nuevo y escribe su provision.json")
		outDir    = flag.String("provision-out", ".", "directorio donde escribir provision.json")
		serverURL = flag.String("provision-url", "", "nombre del servidor que va en provision.json")
		status    = flag.Bool("status", false, "solo informar del estado y salir")
		addUser   = flag.String("add-user", "", "crea o cambia un usuario de la consola y sale")
		deviceID  = flag.Uint64("device-id", 1, "aparato cuyos estados de entrega muestra la consola")
		secureCk  = flag.Bool("secure-cookie", false, "marca la cookie de sesión como Secure (ponlo detrás de Funnel)")
	)
	flag.Parse()

	o := opciones{
		dbPath: *db, listen: *listen, serverID: *serverID, datasetID: *datasetID,
		provisionDev: *provision, outDir: *outDir, serverURL: *serverURL,
		statusOnly: *status, addUser: *addUser, deviceID: *deviceID, secureCookie: *secureCk,
	}
	if err := run(o); err != nil {
		fmt.Fprintf(os.Stderr, "nitr0sd: %v\n", err)
		os.Exit(1)
	}
}

type opciones struct {
	dbPath, listen, outDir, serverURL, addUser  string
	serverID, datasetID, provisionDev, deviceID uint64
	statusOnly, secureCookie                    bool
}

func run(o opciones) error {
	h, err := store.Open(o.dbPath)
	if err != nil {
		return fmt.Errorf("no se pudo abrir el almacén %q: %w", o.dbPath, err)
	}
	defer h.Close()

	if o.addUser != "" {
		return crearUsuario(h, o.addUser)
	}
	if o.provisionDev != 0 {
		return provisionar(h, o.provisionDev, o.outDir, o.serverURL)
	}
	if o.statusOnly {
		return informar(h, o.dbPath)
	}
	if err := h.PurgeSessions(); err != nil {
		return err
	}
	if n, err := h.CountUsers(); err == nil && n == 0 {
		fmt.Println("AVISO: no hay ningún usuario de consola. Crea uno con -add-user <nombre>")
	}

	mux := api.New(h, api.Config{
		ServerID: o.serverID, DatasetID: o.datasetID, Schema: esquemaV1,
	}).Routes()
	cons, err := consola.New(h, consola.Config{
		ServerID: o.serverID, Schema: esquemaV1, DeviceID: o.deviceID,
		SecureCookie: o.secureCookie,
	})
	if err != nil {
		return err
	}
	cons.Rutas(mux)

	srv := &http.Server{
		Addr:    o.listen,
		Handler: mux,
		// Una Pi 3 detrás de Funnel: sin plazos, una conexión colgada se queda
		// para siempre y el servidor se acaba quedando sin descriptores.
		ReadHeaderTimeout: 10 * time.Second,
		ReadTimeout:       60 * time.Second,
		WriteTimeout:      60 * time.Second,
		IdleTimeout:       120 * time.Second,
	}

	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()

	errc := make(chan error, 1)
	go func() {
		fmt.Printf("nitr0sd — %s/%s, go %s\n", runtime.GOOS, runtime.GOARCH, runtime.Version())
		fmt.Printf("protocolo v%d (se aceptan %v), dataset %d, autor del servidor %d\n",
			wire.ProtoVersion, wire.SupportedVersions(), o.datasetID, o.serverID)
		fmt.Printf("escuchando en http://%s (el TLS lo pone Funnel por delante)\n", o.listen)
		errc <- srv.ListenAndServe()
	}()

	select {
	case err := <-errc:
		if errors.Is(err, http.ErrServerClosed) {
			return nil
		}
		return err
	case <-ctx.Done():
		fmt.Println("\nparando; se termina lo que haya en vuelo")
		cctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
		defer cancel()
		return srv.Shutdown(cctx)
	}
}

// crearUsuario pide la contraseña por la entrada estándar para que no quede en
// el historial del shell ni en la lista de procesos.
func crearUsuario(h *store.DB, nombre string) error {
	fmt.Printf("contraseña para %q (se lee de la entrada estándar): ", nombre)
	r := bufio.NewReader(os.Stdin)
	clave, err := r.ReadString('\n')
	if err != nil {
		return err
	}
	clave = strings.TrimRight(clave, "\r\n")
	if len(clave) < 10 {
		return errors.New("la contraseña tiene que tener al menos 10 caracteres")
	}
	if err := h.AddUser(nombre, clave); err != nil {
		return err
	}
	fmt.Printf("usuario %q listo\n", nombre)
	return nil
}

func informar(h *store.DB, dbPath string) error {
	ops, err := h.Ops()
	if err != nil {
		return err
	}
	conf, _ := h.CountConflicts()
	ev, _ := h.CountEvents()
	fmt.Printf("almacén %q (esquema v%d): %d operaciones, %d conflictos, %d eventos\n",
		dbPath, store.SchemaVersion, len(ops), conf, ev)
	return nil
}

// provisionar da de alta un aparato y escribe el fichero que va a la microSD.
//
// La tarjeta es el único canal de entrada que no es un botón, y tener la
// tarjeta en la mano es tener acceso total: por eso el fichero se escribe con
// permisos 0600 y el secreto NO se imprime en pantalla.
func provisionar(h *store.DB, deviceID uint64, outDir, serverURL string) error {
	secret := make([]byte, 32)
	if _, err := rand.Read(secret); err != nil {
		return err
	}
	if err := h.AddSecret(deviceID, secret); err != nil {
		return err
	}
	if serverURL == "" {
		serverURL = "https://CAMBIAME.ts.net"
	}
	doc := map[string]any{
		"device_id":  deviceID,
		"secret_hex": hex.EncodeToString(secret),
		"server_url": serverURL,
		"wifi":       map[string]string{"ssid": "CAMBIAME", "psk": "CAMBIAME"},
		"nota": "Al importarlo, el aparato lo copia a NVS y BORRA o RENOMBRA este fichero. " +
			"Tener la tarjeta en la mano es tener acceso total.",
	}
	b, err := json.MarshalIndent(doc, "", "  ")
	if err != nil {
		return err
	}
	path := filepath.Join(outDir, "provision.json")
	if err := os.WriteFile(path, append(b, '\n'), 0o600); err != nil {
		return err
	}
	fmt.Printf("aparato %d dado de alta; secreto guardado en el almacén\n", deviceID)
	fmt.Printf("escrito %s con permisos 0600\n", path)
	fmt.Println("rellena ssid, psk y server_url, cópialo a la raíz de la microSD y no lo subas a git")
	return nil
}
