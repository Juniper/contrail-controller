package rbac

import (
	"context"
	"testing"
	"time"

	"github.com/Juniper/asf/pkg/auth"
)

func TestCheckCommonPermissions(t *testing.T) {
	type args struct {
		ctx     context.Context
		aaaMode string
		kind    string
		op      Action
	}

	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	tests := []struct {
		name    string
		args    args
		wantErr bool
	}{
		{
			name: "RBAC virtual-network create with no RBAC",
			args: args{
				ctx:     auth.NoAuth(ctx),
				aaaMode: "",
				kind:    "virtual-network",
				op:      ActionCreate,
			},
		},
		{
			name: "RBAC virtual-network create with RBAC enabled as admin",
			args: args{
				ctx:     auth.NoAuth(ctx),
				aaaMode: "rbac",
				kind:    "virtual-network",
				op:      ActionCreate,
			},
		},
		{
			name: "RBAC virtual-network create with RBAC enabled as Member  with RBAC disabled",
			args: args{
				ctx:     userAuth(ctx, "default-project"),
				aaaMode: "",
				kind:    "virtual-network",
				op:      ActionCreate,
			},
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, err := CheckCommonPermissions(tt.args.ctx, tt.args.aaaMode, tt.args.kind, tt.args.op)
			if (err != nil) != tt.wantErr {
				t.Errorf("ChecktCommonPermissions() error = %v, wantErr %v", err, tt.wantErr)
			}
		})
	}
}
func TestCheckPermissions(t *testing.T) {
	type args struct {
		ctx     context.Context
		l       []*APIAccessList
		aaaMode string
		kind    string
		op      Action
	}

	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	tests := []struct {
		name    string
		args    args
		wantErr bool
	}{
		{
			name: "RBAC project create with  RBAC enabled as Member and global RBAC rule",
			args: args{
				ctx:     userAuth(ctx, "default-project"),
				l:       globalAccessRuleList(),
				aaaMode: "rbac",
				kind:    "project",
				op:      ActionCreate,
			},
		},
		{
			name: "RBAC project create with  RBAC enabled as Member and domain RBAC rule",
			args: args{
				ctx:     userAuth(ctx, "default-project"),
				l:       domainAccessRuleList(),
				aaaMode: "rbac",
				kind:    "project",
				op:      ActionCreate,
			},
		},
		{
			name: "RBAC project create with  RBAC enabled as Member and project RBAC rule",
			args: args{
				ctx:     userAuth(ctx, "default-project"),
				l:       projectAccessRuleList(),
				aaaMode: "rbac",
				kind:    "project",
				op:      ActionCreate,
			},
		},
		{
			name: "RBAC project create  and project RBAC rule and shared permissions",
			args: args{
				ctx:     userAuth(ctx, "default-project"),
				l:       projectAccessRuleList(),
				aaaMode: "rbac",
				kind:    "project",
				op:      ActionCreate,
			},
		},
		{
			name: "RBAC project create  and project RBAC rule and shared permissions",
			args: args{
				ctx:     userAuth(ctx, "project_red_uuid"),
				l:       globalAccessRuleList(),
				aaaMode: "rbac",
				kind:    "project",
				op:      ActionCreate,
			},
		},
		{
			name: "RBAC project create with RBAC enabled as Member and global RBAC rule",
			args: args{
				ctx:     userAuth(ctx, "default-project"),
				l:       globalAccessRuleList(),
				aaaMode: "rbac",
				kind:    "project",
				op:      ActionCreate,
			},
		},
		{
			name: "RBAC project create with RBAC enabled as Member and wildcard global RBAC rule",
			args: args{
				ctx:     userAuth(ctx, "default-project"),
				l:       wildcardGlobalAccessRuleList(),
				aaaMode: "rbac",
				kind:    "project",
				op:      ActionCreate,
			},
		},
		{
			name: "RBAC project create with RBAC enabled as Member and domain RBAC rule",
			args: args{
				ctx:     userAuth(ctx, "default-project"),
				l:       domainAccessRuleList(),
				aaaMode: "rbac",
				kind:    "project",
				op:      ActionCreate,
			},
		},
		{
			name: "RBAC project create with RBAC enabled as Member and project RBAC rule",
			args: args{
				ctx:     userAuth(ctx, "default-project"),
				l:       projectAccessRuleList(),
				aaaMode: "rbac",
				kind:    "project",
				op:      ActionCreate,
			},
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			err := CheckPermissions(tt.args.ctx, tt.args.l, tt.args.aaaMode, tt.args.kind, tt.args.op)
			if (err != nil) != tt.wantErr {
				t.Errorf("ChecktPermissions() error = %v, wantErr %v", err, tt.wantErr)
			}
		})
	}
}

func TestCheckObjectPermissions(t *testing.T) {
	type args struct {
		ctx     context.Context
		p       *PermType2
		aaaMode string
		kind    string
		op      Action
	}

	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	tests := []struct {
		name    string
		args    args
		wantErr bool
	}{
		{
			name: "resource  access of owner with full permission",
			args: args{
				ctx: userAuth(ctx, "project_blue_uuid"),
				p: &PermType2{
					Owner:        "project_blue_uuid",
					OwnerAccess:  7,
					GlobalAccess: 0,
					Share:        []*ShareType{{TenantAccess: 7, Tenant: "project:project_red_uuid"}},
				},
				aaaMode: "rbac",
				kind:    "project",
				op:      ActionRead,
			},
		},
		{
			name: "resource read by owner without permission",
			args: args{
				ctx: userAuth(ctx, "project_blue_uuid"),
				p: &PermType2{
					Owner:        "project_blue_uuid",
					OwnerAccess:  2,
					GlobalAccess: 0,
					Share:        []*ShareType{{TenantAccess: 7, Tenant: "project:project_red_uuid"}},
				},
				aaaMode: "rbac",
				kind:    "project",
				op:      ActionRead,
			},
			wantErr: true,
		},
		{
			name: "resource read by owner without permission",
			args: args{
				ctx: userAuth(ctx, "project_blue_uuid"),
				p: &PermType2{
					Owner:        "project_blue_uuid",
					OwnerAccess:  4,
					GlobalAccess: 0,
					Share:        []*ShareType{{TenantAccess: 7, Tenant: "project:project_red_uuid"}},
				},
				aaaMode: "rbac",
				kind:    "project",
				op:      ActionRead,
			},
		},

		{
			name: "resource access by the shared project with permission",
			args: args{
				ctx: userAuth(ctx, "project_red_uuid"),
				p: &PermType2{
					Owner:        "project_blue_uuid",
					OwnerAccess:  7,
					GlobalAccess: 0,
					Share:        []*ShareType{{TenantAccess: 7, Tenant: "project:project_red_uuid"}},
				},
				aaaMode: "rbac",
				kind:    "project",
				op:      ActionUpdate,
			},
		},
		{
			name: "Project access by no shared project",
			args: args{
				ctx: userAuth(ctx, "default-project"),
				p: &PermType2{
					Owner:        "project_blue_uuid",
					OwnerAccess:  7,
					GlobalAccess: 0,
					Share:        []*ShareType{{TenantAccess: 7, Tenant: "project:project_red_uuid"}},
				},
				aaaMode: "rbac",
				kind:    "project",
				op:      ActionDelete,
			},
			wantErr: true,
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			err := CheckObjectPermissions(tt.args.ctx, tt.args.p, tt.args.aaaMode, tt.args.kind, tt.args.op)
			if (err != nil) != tt.wantErr {
				t.Errorf("CheckPermissions() error = %v, wantErr %v", err, tt.wantErr)
			}
		})
	}
}

func userAuth(ctx context.Context, tenant string) context.Context {
	Context := auth.NewContext(
		"default-domain", tenant, "bob", []string{"Member"}, "", auth.NewObjPerms(nil))
	return auth.WithIdentity(ctx, Context)
}

func globalAccessRuleList() []*APIAccessList {
	list := make([]*APIAccessList, 0)
	model := makeAPIAccessList()
	model.FQName = []string{"default-global-system-config"}
	model.ParentType = KindGlobalSystemConfig
	rbacRuleEntryAdd(model, "projects")
	return append(list, model)
}

func domainAccessRuleList() []*APIAccessList {
	list := make([]*APIAccessList, 0)
	model := makeAPIAccessList()
	model.FQName = []string{"default-domain"}
	model.ParentType = KindDomain
	rbacRuleEntryAdd(model, "projects")
	return append(list, model)
}

func projectAccessRuleList() []*APIAccessList {
	list := make([]*APIAccessList, 0)
	model := makeAPIAccessList()
	model.FQName = []string{"default-domain", "default-project"}
	model.ParentType = KindProject
	rbacRuleEntryAdd(model, "projects")
	return append(list, model)
}

func wildcardGlobalAccessRuleList() []*APIAccessList {
	list := make([]*APIAccessList, 0)
	model := makeAPIAccessList()
	model.FQName = []string{"default-global-system-config"}
	model.ParentType = KindGlobalSystemConfig
	rbacRuleEntryAdd(model, "*")
	return append(list, model)
}

func makeAPIAccessList() *APIAccessList {
	return &APIAccessList{
		ParentType:           "",
		FQName:               []string{},
		APIAccessListEntries: makeRbacRuleEntriesType(),
	}
}

func makeRbacRuleEntriesType() *RuleEntriesType {
	return &RuleEntriesType{
		Rule: makeRbacRuleTypeSlice(),
	}
}

func makeRbacRuleTypeSlice() []*RuleType {
	return []*RuleType{}
}

func rbacRuleEntryAdd(l *APIAccessList, kind string) {
	m := makeRbacRuleType()
	m.RuleObject = kind
	p := makeRbacPermType()
	p.RoleCrud = "CRUD"
	p.RoleName = "Member"
	m.RulePerms = append(m.RulePerms, p)
	l.APIAccessListEntries.Rule = append(l.APIAccessListEntries.Rule, m)
}

func makeRbacRuleType() *RuleType {
	return &RuleType{
		RuleObject: "",
		RulePerms: makeRbacPermTypeSlice(),
	}
}

func makeRbacPermTypeSlice() []*PermType {
	return []*PermType{}
}

func makeRbacPermType() *PermType {
	return &PermType{
		RoleCrud: "",
		RoleName: "",
	}
}
