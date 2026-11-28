package api_test

import (
	"bytes"
	"io"
	"testing"
)

func bytesReader(b []byte) io.Reader { return bytes.NewReader(b) }

func leerTodo(t *testing.T, r io.Reader) []byte {
	t.Helper()
	b, err := io.ReadAll(r)
	if err != nil {
		t.Fatal(err)
	}
	return b
}
