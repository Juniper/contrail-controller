package keystone

import (
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestAuth(t *testing.T) {
	auth := NewAuthIdentity("default", "admin", "admin", []string{"admin"})

	assert.Equal(t, auth.IsAdmin(), true)
	assert.Equal(t, auth.ProjectID(), "admin")
	assert.Equal(t, auth.DomainID(), "default")

	auth = NewAuthIdentity(
		"default", "demo", "demo", []string{})

	assert.Equal(t, auth.IsAdmin(), false)
	assert.Equal(t, auth.ProjectID(), "demo")
	assert.Equal(t, auth.DomainID(), "default")

	auth = NewAuthIdentity(
		"default", "demo", "demo", []string{}, WithToken("authtoken"))

	assert.Equal(t, auth.IsAdmin(), false)
	assert.Equal(t, auth.ProjectID(), "demo")
	assert.Equal(t, auth.DomainID(), "default")
	assert.Equal(t, auth.AuthToken(), "authtoken")
}
