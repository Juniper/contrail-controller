package keystone

import (
	"github.com/Juniper/asf/pkg/auth"
	"github.com/Juniper/asf/pkg/format"
)

// authIdentity is used to represent keystone identity.
type authIdentity struct {
	projectID string
	domainID  string
	userID    string
	roles     []string

	authToken   string
	objectPerms ObjectPerms
}

// Role values
const (
	AdminRole          = "admin"
	GlobalReadOnlyRole = "RO"
	CloudAdminRole     = "cloud_admin"
)

// IdentityOption sets optional params on authIdentity.
type IdentityOption func(*authIdentity)

func WithToken(token string) IdentityOption {
	return func(c *authIdentity) {
		c.authToken = token
	}
}

func WithObjectPerms(p ObjectPerms) IdentityOption {
	return func(c *authIdentity) {
		c.objectPerms = p
	}
}

// NewAuthIdentity makes a authentication context.
func NewAuthIdentity(
	domainID, projectID, userID string, roles []string, opts ...IdentityOption,
) auth.Identity {
	id := &authIdentity{
		projectID: projectID,
		domainID:  domainID,
		userID:    userID,
		roles:     roles,
	}
	for _, opt := range opts {
		opt(id)
	}
	id.substituteObjectPerms()
	return id
}

func (id *authIdentity) substituteObjectPerms() {
	id.objectPerms.IsGlobalReadOnlyRole = id.IsGlobalRORole()
	id.objectPerms.IsCloudAdminRole = id.IsCloudAdminRole()
	id.objectPerms.TokenInfo.Token.AuthToken = id.AuthToken()
}

// GetObjPerms returns object perms
func (id *authIdentity) GetObjPerms() interface{} {
	return id.objectPerms
}

// IsAdmin is used to check if this is admin id
// TODO(mblotniak): change this into func(auth.Identity) bool and move it to asf/pkg/auth
func (id *authIdentity) IsAdmin() bool {
	return format.ContainsString(id.roles, AdminRole)
}

// IsGlobalRORole is used to check if this id is  global read only role
// TODO(mblotniak): change this into func(auth.Identity) bool and move it to asf/pkg/auth
func (id *authIdentity) IsGlobalRORole() bool {
	return format.ContainsString(id.roles, GlobalReadOnlyRole)
}

// IsCloudAdminRole is used to check if this id is cloud admin role
// TODO(mblotniak): change this into func(auth.Identity) bool and move it to asf/pkg/auth
func (id *authIdentity) IsCloudAdminRole() bool {
	return format.ContainsString(id.roles, CloudAdminRole)
}

// ProjectID is used to get an id for project.
func (id *authIdentity) ProjectID() string {
	return id.projectID
}

// DomainID is used to get an id for domain.
func (id *authIdentity) DomainID() string {
	return id.domainID
}

// UserID is used to get an id for User.
func (id *authIdentity) UserID() string {
	return id.userID
}

// AuthToken is used to get an auth token of request.
func (id *authIdentity) AuthToken() string {
	return id.authToken
}

// Roles  is used to get the roles of a user
func (id *authIdentity) Roles() []string {
	return id.roles
}
