package rbac

// Action enumerates CRUD actions.
type Action int

// Enumeration of Actions.
const (
	ActionInvalid Action = iota
	ActionCreate
	ActionRead
	ActionUpdate
	ActionDelete
)

// APIAccessList holds permissions data.
type APIAccessList struct {
	ParentType           string
	FQName               []string
	APIAccessListEntries *RuleEntriesType
}

// RuleEntriesType holds permissions data.
type RuleEntriesType struct {
	Rule []*RuleType
}

// RuleType holds permissions data.
type RuleType struct {
	RuleObject string
	RulePerms  []*PermType
}

// PermType holds permissions data.
type PermType struct {
	RoleCrud string
	RoleName string
}

func (r *PermType) getRoleCrud() string {
	if r != nil {
		return r.RoleCrud
	}
	return ""
}

func (r *PermType) getRoleName() string {
	if r != nil {
		return r.RoleName
	}
	return ""
}

func (r *RuleType) getRuleObject() string {
	if r != nil {
		return r.RuleObject
	}
	return ""
}

func (r *RuleType) getRulePerms() []*PermType {
	if r != nil {
		return r.RulePerms
	}
	return nil
}

func (r *RuleEntriesType) getRuleType() []*RuleType {
	if r != nil {
		return r.Rule
	}
	return nil
}

func (a *APIAccessList) getParentType() string {
	if a != nil {
		return a.ParentType
	}
	return ""
}

func (a *APIAccessList) getFQName() []string {
	if a != nil {
		return a.FQName
	}
	return nil
}

func (a *APIAccessList) getAPIAccessListEntries() *RuleEntriesType {
	if a != nil {
		return a.APIAccessListEntries
	}
	return nil
}

// PermType2 holds object level (perms2) permissions.
type PermType2 struct {
	Owner        string
	OwnerAccess  int64
	GlobalAccess int64
	Share        []*ShareType
}

// ShareType is used to check whether a resource is shared with the tenant.
type ShareType struct {
	TenantAccess int64
	Tenant       string
}

func (s *ShareType) getTenantAccess() int64 {
	if s != nil {
		return s.TenantAccess
	}
	return 0
}

func (s *ShareType) getTenant() string {
	if s != nil {
		return s.Tenant
	}
	return ""
}

func (p *PermType2) getOwner() string {
	if p != nil {
		return p.Owner
	}
	return ""
}

func (p *PermType2) getOwnerAccess() int64 {
	if p != nil {
		return p.OwnerAccess
	}
	return 0
}

func (p *PermType2) getGlobalAccess() int64 {
	if p != nil {
		return p.GlobalAccess
	}
	return 0
}

func (p *PermType2) getShare() []*ShareType {
	if p != nil {
		return p.Share
	}
	return nil
}
