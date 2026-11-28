// nitr0sd es el servidor de nitr0S: un binario único con SQLite dentro.
//
// Todavía no sirve HTTP: de momento abre el almacén y dice qué hay, que es lo
// que hace falta para la primera prueba de despliegue en la Pi.
package main

import (
	"flag"
	"fmt"
	"os"
	"runtime"

	"github.com/aitorTD/nitr0S/servidor/internal/store"
	"github.com/aitorTD/nitr0S/servidor/internal/wire"
)

func main() {
	db := flag.String("db", "nitr0s.db", "ruta del fichero SQLite")
	flag.Parse()

	fmt.Printf("nitr0sd — %s/%s, go %s\n", runtime.GOOS, runtime.GOARCH, runtime.Version())
	fmt.Printf("protocolo de replicación: v%d (se aceptan %v)\n",
		wire.ProtoVersion, wire.SupportedVersions())

	h, err := store.Open(*db)
	if err != nil {
		fmt.Fprintf(os.Stderr, "no se pudo abrir el almacén %q: %v\n", *db, err)
		os.Exit(1)
	}
	defer h.Close()

	ops, err := h.Ops()
	if err != nil {
		fmt.Fprintf(os.Stderr, "no se pudo leer el registro: %v\n", err)
		os.Exit(1)
	}
	conf, _ := h.CountConflicts()
	ev, _ := h.CountEvents()
	fmt.Printf("almacén %q (esquema v%d): %d operaciones, %d conflictos, %d eventos\n",
		*db, store.SchemaVersion, len(ops), conf, ev)
}
