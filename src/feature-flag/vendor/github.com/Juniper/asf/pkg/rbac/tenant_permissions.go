package rbac

import (
	"strings"

	"github.com/Juniper/asf/pkg/errutil"
)

// tenantPermissions is the 1st level hash table to get tenant specific rules.
type tenantPermissions map[tenantKey]resourcePermissions

type tenantKey string

// AddAccessList creates project, domain and  global policies.
func (t tenantPermissions) AddAccessList(lists []*APIAccessList) error {
	global := make([]*APIAccessList, 0)
	domains := make(map[string][]*APIAccessList)
	projects := make(map[tenantKey][]*APIAccessList)

	for _, list := range lists {

		parentType := list.getParentType()
		fqName := list.getFQName()

		switch parentType {
		case globalSystemConfigScope:
			global = append(global, list)
		case domainScope:
			domainName := fqName[0]
			domains[domainName] = append(domains[domainName], list)
		case projectScope:
			domainName, projectName := fqName[0], fqName[1]
			key := getTenantKey(domainName, projectName)
			projects[key] = append(projects[key], list)
		default:
			return errutil.ErrorInternalf("invalid parent type present in RBAC rule.")
		}
	}

	err := t.addTenantLevelPolicy(global, domains, projects)
	if err != nil {
		return err
	}

	err = t.addDomainLevelPolicy(global, domains)
	if err != nil {
		return err
	}

	err = t.addGlobalLevelPolicy(global)
	if err != nil {
		return err
	}
	return nil
}

// addTenantLevelPolicy create a project specific policy.
func (t tenantPermissions) addTenantLevelPolicy(
	global []*APIAccessList,
	domain map[string][]*APIAccessList,
	project map[tenantKey][]*APIAccessList,
) error {
	for k, list := range project {

		s := strings.Split(string(k), ".")
		domainName := s[0]
		domainList := domain[domainName]
		list = append(list, global...)
		list = append(list, domainList...)
		entry, err := t.addAPIAccessRules(list)
		if err != nil {
			return err
		}
		t[k] = entry
	}
	return nil
}

// addDomainLevelPolicy create a domain specific policy.
func (t tenantPermissions) addDomainLevelPolicy(
	global []*APIAccessList, domain map[string][]*APIAccessList) error {
	for k, list := range domain {
		list = append(list, global...)
		key := getTenantKey(k, "*")
		entry, err := t.addAPIAccessRules(list)
		if err != nil {
			return err
		}
		t[key] = entry
	}
	return nil
}

// addGlobalLevelPolicy create a  global policy.
func (t tenantPermissions) addGlobalLevelPolicy(global []*APIAccessList) error {
	key := getTenantKey("*", "*")
	entry, err := t.addAPIAccessRules(global)
	if err != nil {
		return err
	}
	t[key] = entry
	return nil
}

// addAPIAccessRules adds rules matching (resource )objectKind and wild card rules from the rule list to
// resourcePermissions table.
// For example input apiAccessRules may contains rules from global-config, default-domain & project
// different all resource types. There can be multiple roles and CRUD for every resource type.
// Following  is an input example.
//
// Rule 1
// 1)  <global-config, virtual-network, network-policy> => Member:CRUD
// Rule 2
// 2)  <default-domain,virtual-network, network-ipam> => Development:CRUD
// Rule 3
// 1)  <project,virtual-ip, *>		 => Member:CRUD
// 2)  <project,virtual-network, *>     => Member:CRUD, Development:CRUD
//
// This Function will do the following
// i) Filter Rules based on project and domain
// ii) Create request.hash in the following form
//  objectKind.* ==>{ role1 : actionSet1  ,  role2 : actionSet2 }
// For example result could be
// virtual-network.* ==> { Member 	 : { ActionCreate:true,ActionDelete:true  }  ,
//			    Development  : { ActionUpdate:true } }
func (t tenantPermissions) addAPIAccessRules(apiAccessRules []*APIAccessList) (resourcePermissions, error) {
	res := make(resourcePermissions)
	for _, apiRule := range apiAccessRules {

		rbacRules := getRBACRules(apiRule)
		err := res.RulesAdd(rbacRules)
		if err != nil {
			return nil, err
		}
	}

	return res, nil
}

// getRBACRules retrieves RBAC rules from API access list entry.
func getRBACRules(l *APIAccessList) []*RuleType {
	return l.getAPIAccessListEntries().getRuleType()
}

// ValidateAPILevel checks whether any resource rules or wildcard allow this operation
// for any roles which user is part of.
func (t *tenantPermissions) ValidateAPILevel(rq *request) bool {
	// check for Resource  rule .
	if t.validateResourceAPILevel(rq, rq.kind) {
		return true
	}

	// check for wild card rule
	if t.validateResourceAPILevel(rq, "*") {
		return true
	}
	return false
}

// validateResourceAPILevel checks whether any hash entry is matching the key.
func (t *tenantPermissions) validateResourceAPILevel(rq *request, kind string) bool {
	r := t.getResourcePermission(rq)
	if r == nil {
		return false
	}

	key := getResourceKey(kind)
	if val, ok := r[key]; ok {
		return val.IsRuleMatching(rq.roles, rq.op)
	}

	return false
}

// getResourcePermission retrieves matching first level hash table entry .
func (t tenantPermissions) getResourcePermission(rq *request) resourcePermissions {
	key := getTenantKey(rq.domain, rq.project)

	if val, ok := t[key]; ok {
		return val
	}
	key = getTenantKey(rq.domain, "*")

	if val, ok := t[key]; ok {
		return val
	}
	key = getTenantKey("*", "*")

	if val, ok := t[key]; ok {
		return val
	}

	return nil
}

// getTenantKey get the Hash key for first level hash.
func getTenantKey(domainName string, projectName string) tenantKey {
	return tenantKey(domainName + "." + projectName)
}
