package rbac

import (
	"strings"

	"github.com/Juniper/asf/pkg/errutil"
)

// resourcePermissions is the 2st level hash table to get objectKind specific rules.
type resourcePermissions map[resourceKey]allowedActions

type resourceKey string

// RulesAdd adds  matching the objectKind or wildcard RBAC rules to hash.
// Input is in the  following form
// < virtual-network, *> => Member:CRUD ,Development:CRUD
// < virtual-ip, *> => Member:CRUD
func (r resourcePermissions) RulesAdd(rbacRules []*RuleType) error {
	for _, obj := range rbacRules {

		err := r.objectKindRuleAdd(obj)
		if err != nil {
			return err
		}
	}
	return nil
}

// objectKindRuleAdd add objectKind or wildcard RBAC rules to hash.
// Input rbacRule is in the the following form
// < virtual-network, *> => Member:CRUD ,Development:CRUD.
func (r resourcePermissions) objectKindRuleAdd(rbacRule *RuleType) error {
	if rbacRule == nil {
		return nil
	}

	ruleKind := pluralNameToKind(rbacRule.getRuleObject())
	perms := rbacRule.getRulePerms()

	err := r.objectKindPermsAdd(ruleKind, perms)
	if err != nil {
		return err
	}
	return nil
}

// pluralNameToKind converts plural object name to object kind.  eg.virtual-networks => virtual-network.
func pluralNameToKind(name string) string {
	return strings.TrimSuffix(name, "s")
}

// objectKindPermsAdd adds objectKind or wildcard RBAC rules to hash.
// Input "perms" is in the  following form
// Member:CRUD ,Development:CRUD
func (r resourcePermissions) objectKindPermsAdd(ruleKind string, perms []*PermType) error {
	for _, perm := range perms {
		err := r.objectKindPermAdd(ruleKind, perm)
		if err != nil {
			return err
		}
	}
	return nil
}

// objectKindPermsAdd adds objectKind or wildcard RBAC rules to hash.
// Input "perm" is in the following form
// Member:CRUD
func (r resourcePermissions) objectKindPermAdd(ruleKind string, perm *PermType) error {
	if perm == nil {
		return nil
	}
	role := perm.getRoleName()
	crud, err := crudToActionSet(perm.getRoleCrud())
	if err != nil {
		return err
	}
	r.updateRule(ruleKind, role, crud)
	return nil
}

// crudToActionSet converts crud string to op slice.
func crudToActionSet(crud string) ([]Action, error) {
	var res []Action

	for _, c := range crud {
		switch c {
		case upperCaseC:
			res = append(res, ActionCreate)
		case upperCaseR:
			res = append(res, ActionRead)
		case upperCaseU:
			res = append(res, ActionUpdate)
		case upperCaseD:
			res = append(res, ActionDelete)
		default:
			return nil, errutil.ErrorInternalf("invalid crud action present in RBAC rule.")
		}
	}
	return res, nil
}

// updateRule adds rules to the resourcePermissions hash table. resourcePermissions hash table will have
// entries of the form  key => {{role1:actionSet1},{role2:actionSet2}}. This will merge the resource CRUD in
// global,domain and  project level.
func (r resourcePermissions) updateRule(ruleKind string, role string, crud []Action) {
	key := getResourceKey(ruleKind)

	if val, ok := r[key]; ok {
		val.UpdateEntry(role, crud)

	} else {
		val := newAllowedActions()
		val.UpdateEntry(role, crud)
		r[key] = val
	}

	return
}

// getResourceKey forms a hash key from objName,objField.
func getResourceKey(kind string) (key resourceKey) {
	// Not implemented field name based rules yet
	return resourceKey(kind + "." + "*")
}

// allowedActions is a hash table of roles and it's allowed actions.
type allowedActions map[string]actionSet

func newAllowedActions() allowedActions {
	return make(map[string]actionSet)
}

// IsRuleMatching checks whether there is any rule is allowing this op.
func (m allowedActions) IsRuleMatching(roleSet map[string]bool, op Action) bool {
	// Check against all user roles
	for role := range roleSet {
		if cs, ok := m[role]; ok {

			return cs.IsActionAllowed(op)
		}
	}
	// Check against wild card role rule
	if cs, ok := m["*"]; ok {

		return cs.IsActionAllowed(op)
	}
	return false
}

// UpdateEntry adds a new role to action  mapping entry. For example, "Member" ==> "CR".
func (m allowedActions) UpdateEntry(role string, crud []Action) {
	if cs, ok := m[role]; ok {
		cs.SetCRUD(crud)
	} else {
		n := newActionSet()
		n.SetCRUD(crud)
		m[role] = n
	}
	return
}

type actionSet map[Action]struct{}

// newActionSet create a new actionSet.
func newActionSet() actionSet {
	return make(map[Action]struct{})
}

// IsActionAllowed checks whether an operation is is present in actionSet.
func (s actionSet) IsActionAllowed(op Action) bool {
	if _, ok := s[op]; ok {
		return true

	}
	return false
}

// SetCRUD add an operation to actionSet.
func (s actionSet) SetCRUD(crud []Action) {
	for _, op := range crud {
		s[op] = struct{}{}
	}
	return
}
