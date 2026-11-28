module github.com/aitorTD/nitr0S/herramientas

go 1.24

// Fijada: las versiones actuales de x/image exigen Go 1.26 y harían que el
// build se descargara un toolchain solo, igual que pasó con sqlite.
require golang.org/x/image v0.23.0

require golang.org/x/text v0.21.0 // indirect
