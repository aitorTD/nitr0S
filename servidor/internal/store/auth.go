package store

import (
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"database/sql"
	"errors"
	"time"

	"crypto/pbkdf2"
)

// Autenticación humana: usuario y contraseña. PBKDF2 de la biblioteca estándar
// de Go 1.24, así que no entra ninguna dependencia nueva por esto.
const pbkdf2Iter = 600_000

var ErrBadLogin = errors.New("store: usuario o contraseña incorrectos")

func derive(password string, salt []byte) ([]byte, error) {
	return pbkdf2.Key(sha256.New, password, salt, pbkdf2Iter, 32)
}

func (d *DB) AddUser(username, password string) error {
	salt := make([]byte, 16)
	if _, err := rand.Read(salt); err != nil {
		return err
	}
	h, err := derive(password, salt)
	if err != nil {
		return err
	}
	_, err = d.sql.Exec(`INSERT OR REPLACE INTO users (username,salt,hash,created_at)
		VALUES (?,?,?,?)`, username, salt, h, time.Now().Unix())
	return err
}

func (d *DB) CountUsers() (int, error) {
	var n int
	err := d.sql.QueryRow(`SELECT COUNT(*) FROM users`).Scan(&n)
	return n, err
}

// Login verifica la contraseña en tiempo constante y abre una sesión.
func (d *DB) Login(username, password string, ttl time.Duration) ([]byte, error) {
	var salt, want []byte
	err := d.sql.QueryRow(`SELECT salt,hash FROM users WHERE username=?`, username).Scan(&salt, &want)
	if err == sql.ErrNoRows {
		// Se deriva igualmente para no filtrar por tiempo si el usuario existe.
		_, _ = derive(password, make([]byte, 16))
		return nil, ErrBadLogin
	}
	if err != nil {
		return nil, err
	}
	got, err := derive(password, salt)
	if err != nil {
		return nil, err
	}
	if subtle.ConstantTimeCompare(got, want) != 1 {
		return nil, ErrBadLogin
	}
	tok := make([]byte, 32)
	if _, err := rand.Read(tok); err != nil {
		return nil, err
	}
	if _, err := d.sql.Exec(`INSERT INTO sessions (token,username,expires_at) VALUES (?,?,?)`,
		tok, username, time.Now().Add(ttl).Unix()); err != nil {
		return nil, err
	}
	return tok, nil
}

// Session devuelve el usuario de un token vigente.
func (d *DB) Session(token []byte) (string, bool) {
	var user string
	var exp int64
	err := d.sql.QueryRow(`SELECT username,expires_at FROM sessions WHERE token=?`,
		token).Scan(&user, &exp)
	if err != nil || exp < time.Now().Unix() {
		return "", false
	}
	return user, true
}

func (d *DB) Logout(token []byte) error {
	_, err := d.sql.Exec(`DELETE FROM sessions WHERE token=?`, token)
	return err
}

// PurgeSessions borra las caducadas. Se llama al arrancar.
func (d *DB) PurgeSessions() error {
	_, err := d.sql.Exec(`DELETE FROM sessions WHERE expires_at < ?`, time.Now().Unix())
	return err
}
