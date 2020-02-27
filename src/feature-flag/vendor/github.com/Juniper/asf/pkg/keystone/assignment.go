package keystone

import (
	"github.com/pkg/errors"

	"github.com/Juniper/asf/pkg/format"
)

//StaticAssignment is an implementation of Assignment based on static file.
type StaticAssignment struct {
	Domains  map[string]*Domain  `json:"domains"`
	Projects map[string]*Project `json:"projects"`
	Users    map[string]*User    `json:"users"`
}

//ListUsers is used to fetch all users
func (sa *StaticAssignment) ListUsers() (users []*User) {
	for _, user := range sa.Users {
		users = append(users, user)
	}
	return users
}

//FetchUser is used to fetch a user by ID and Password.
func (sa *StaticAssignment) FetchUser(name, password string) (*User, error) {
	user, ok := sa.Users[name]
	if !ok {
		return nil, errors.Errorf("user %s not found", name)
	}
	if user.Password != "" && format.InterfaceToString(user.Password) != password {
		return nil, errors.Errorf("invalid credentials")
	}
	return user, nil
}

//ListDomains is used to list domains
func (sa *StaticAssignment) ListDomains() (domains []*Domain) {
	for _, domain := range sa.Domains {
		domains = append(domains, domain)
	}
	return domains
}

//ListProjects is used to list projects
func (sa *StaticAssignment) ListProjects() (projects []*Project) {
	for _, project := range sa.Projects {
		projects = append(projects, project)
	}
	return projects
}
