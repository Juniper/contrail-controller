package auth

import (
	"context"
)

// Identity describes identity of API user.
type Identity interface {
	GetObjPerms() interface{}
	IsAdmin() bool
	IsGlobalRORole() bool
	IsCloudAdminRole() bool
	ProjectID() string
	DomainID() string
	UserID() string
	AuthToken() string
	Roles() []string
}

type authContextKey string

const (
	authIdentityContextKey authContextKey = "auth"
)

// WithIdentity returns context with given identity.
func WithIdentity(ctx context.Context, id Identity) context.Context {
	return context.WithValue(ctx, authIdentityContextKey, id)
}

// GetIdentity is used to retrieve an identity from given context.
func GetIdentity(ctx context.Context) Identity {
	c, ok := ctx.Value(authIdentityContextKey).(Identity)
	if !ok || c == nil {
		return defaultIdentity{}
	}
	return c
}

const (
	AdminRole = "admin"
)

type defaultIdentity struct{}

// TODO(mblotniak): do not assume admin
func (d defaultIdentity) GetObjPerms() interface{} { return nil }
func (d defaultIdentity) IsAdmin() bool            { return true }
func (d defaultIdentity) IsGlobalRORole() bool     { return true }
func (d defaultIdentity) IsCloudAdminRole() bool   { return true }
func (d defaultIdentity) ProjectID() string        { return AdminRole }
func (d defaultIdentity) DomainID() string         { return AdminRole } // TODO(mblotniak): Verify correctness
func (d defaultIdentity) UserID() string           { return AdminRole }
func (d defaultIdentity) AuthToken() string        { return "" }
func (d defaultIdentity) Roles() []string          { return nil }

// NoAuth returns context with admin identity.
func NoAuth(ctx context.Context) context.Context {
	return WithIdentity(ctx, noAuthIdentity{})
}

// TODO(mblotniak): consider unifying with defaultIdentity
type noAuthIdentity struct{}

func (d noAuthIdentity) GetObjPerms() interface{} { return nil }
func (d noAuthIdentity) IsAdmin() bool            { return true }
func (d noAuthIdentity) IsGlobalRORole() bool     { return true }
func (d noAuthIdentity) IsCloudAdminRole() bool   { return true }
func (d noAuthIdentity) ProjectID() string        { return "default-project" }
func (d noAuthIdentity) DomainID() string         { return "default-domain" }
func (d noAuthIdentity) UserID() string           { return AdminRole }
func (d noAuthIdentity) AuthToken() string        { return "" }
func (d noAuthIdentity) Roles() []string          { return []string{AdminRole} }
