package keystone

// GetDomain gets domain
func (s *Scope) GetDomain() *Domain {
	if s == nil {
		return nil
	}
	if s.Domain != nil {
		return s.Domain
	} else if s.Project != nil {
		return s.Project.Domain
	}
	return nil
}

// NewScope returns the project/domain scope
func NewScope(domainID, domainName, projectID, projectName string) *Scope {
	scope := &Scope{
		Project: &Project{
			ID:   projectID,
			Name: projectName,
			Domain: &Domain{
				ID:   domainID,
				Name: domainName,
			},
		},
	}
	return scope
}
