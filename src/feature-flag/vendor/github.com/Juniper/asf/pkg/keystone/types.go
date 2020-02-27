package keystone

import "time"

//AuthRequest is used to request an authentication.
type AuthRequest interface {
	SetCredential(string, string)
	GetIdentity() *Identity
	GetScope() *Scope
}

//UnScopedAuthRequest is used to request an authentication.
type UnScopedAuthRequest struct {
	Auth *UnScopedAuth `json:"auth"`
}

//SetCredential uses given user in the auth request
func (u UnScopedAuthRequest) SetCredential(user, password string) {
	u.GetIdentity().Password.User.Name = user
	u.GetIdentity().Password.User.Password = password
}

//GetIdentity is to get the identify details from the token reques
func (u UnScopedAuthRequest) GetIdentity() *Identity {
	if u.Auth != nil {
		return u.Auth.Identity
	}
	return nil
}

//GetScope is to get the scope details from the token reques
func (u UnScopedAuthRequest) GetScope() *Scope {
	return nil
}

//UnScopedAuth is used to request an authentication.
type UnScopedAuth struct {
	Identity *Identity `json:"identity"`
}

//ScopedAuthRequest is used to request an authentication.
type ScopedAuthRequest struct {
	Auth *ScopedAuth `json:"auth"`
}

//SetCredential uses given user in the auth request
func (s ScopedAuthRequest) SetCredential(user, password string) {
	s.Auth.Identity.Password.User.Name = user
	s.Auth.Identity.Password.User.Password = password
}

//GetIdentity is to get the identify details from the token reques
func (s ScopedAuthRequest) GetIdentity() *Identity {
	if s.Auth != nil {
		return s.Auth.Identity
	}
	return nil
}

//GetScope is to get the scope details from the token reques
func (s ScopedAuthRequest) GetScope() *Scope {
	if s.Auth != nil {
		return s.Auth.Scope
	}
	return nil
}

//ScopedAuth is used to request an authentication.
type ScopedAuth struct {
	Identity *Identity `json:"identity"`
	Scope    *Scope    `json:"scope"`
}

//Scope is used to limit scope of auth request.
type Scope struct {
	Domain  *Domain  `json:"domain,omitempty"`
	Project *Project `json:"project,omitempty"`
}

// Domain field default values.
const (
	DefaultDomainID   = "default"
	DefaultDomainName = "Default"
)

//Domain represents domain object.
type Domain struct {
	ID   string `json:"id,omitempty"`
	Name string `json:"name,omitempty"`
}

// DefaultDomain constructs domain with default values.
func DefaultDomain() *Domain {
	return &Domain{
		ID:   DefaultDomainID,
		Name: DefaultDomainName,
	}
}

//Project represents project object.
type Project struct {
	Domain   *Domain `json:"domain,omitempty"`
	ID       string  `json:"id,omitempty"`
	Name     string  `json:"name,omitempty"`
	ParentID string  `json:"parent_id,omitempty"`
}

// Projects holds pointers to multiple project objects.
type Projects []*Project

// Find returns the first project that matches the predicate or nil.
func (pp Projects) Find(pred func(*Project) bool) *Project {
	for _, p := range pp {
		if pred(p) {
			return p
		}
	}
	return nil
}

// FindByName returns the first project with the given name or nil.
func (pp Projects) FindByName(name string) *Project {
	return pp.Find(func(p *Project) bool {
		return p.Name == name
	})
}

//Identity represents a auth methods.
type Identity struct {
	Methods  []string   `json:"methods"`
	Password *Password  `json:"password,omitempty"`
	Token    *UserToken `json:"token,omitempty"`
	Cluster  *Cluster   `json:"cluster,omitempty"`
}

//Password represents a password.
type Password struct {
	User *User `json:"user,omitempty"`
}

//AuthResponse represents a authentication response.
type AuthResponse struct {
	Token *Token `json:"token"`
}

//User reprenetns a user.
type User struct {
	Domain   *Domain `json:"domain,omitempty"`
	ID       string  `json:"id"`
	Name     string  `json:"name"`
	Password string  `json:"password"`
	Email    string  `json:"email"`
	Roles    []*Role `json:"roles"`
}

//HasCredential is to check the presence of credential
func (u *User) HasCredential() bool {
	return u.Name != "" || u.Password != ""
}

//Role represents a user role.
type Role struct {
	ID      string   `json:"id"`
	Name    string   `json:"name"`
	Project *Project `json:"project"`
}

//Cluster represent a cluster object sent by user
//to get new token using cluster token
type Cluster struct {
	ID    string     `json:"id"`
	Token *UserToken `json:"token,omitempty"`
}

//UserToken represent a token object sent by user to get new token
type UserToken struct {
	ID string `json:"id"`
	Token
}

//Token represents a token object.
type Token struct {
	AuditIds  []string   `json:"audit_ids"`
	Catalog   []*Catalog `json:"catalog"`
	Domain    *Domain    `json:"domain"`
	Project   *Project   `json:"project"`
	User      *User      `json:"user"`
	ExpiresAt time.Time  `json:"expires_at"`
	IssuedAt  time.Time  `json:"issued_at"`
	Methods   []string   `json:"methods"`
	Roles     []*Role    `json:"roles"`
}

//Catalog represents API catalog.
type Catalog struct {
	Endpoints []*Endpoint `json:"endpoints"`
	ID        string      `json:"id"`
	Name      string      `json:"name"`
	Type      string      `json:"type"`
}

//Endpoint represents API endpoint.
type Endpoint struct {
	ID        string `json:"id"`
	Interface string `json:"interface"`
	Region    string `json:"region"`
	URL       string `json:"url"`
}

//ValidateTokenResponse represents a response object for validate token request.
type ValidateTokenResponse struct {
	Token *Token `json:"token"`
}

// CreateUserRequest represents a keystone user creation request.
type CreateUserRequest struct {
	User `json:"user"`
}

// CreateUserResponse represents a keystone user creation response.
type CreateUserResponse CreateUserRequest

//ProjectResponse represents a project get response.
type ProjectResponse struct {
	Project *Project `json:"project"`
}

//ProjectListResponse represents a project list response.
type ProjectListResponse struct {
	Projects `json:"projects"`
}

//DomainListResponse represents a domain list response.
type DomainListResponse struct {
	Domains []*Domain `json:"domains"`
}
