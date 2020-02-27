package db

import (
	"context"
	"testing"

	"github.com/sirupsen/logrus"

	"github.com/gogo/protobuf/proto"
	"github.com/gogo/protobuf/types"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/Juniper/asf/pkg/db/basedb"
	"github.com/Juniper/asf/pkg/models"
	"github.com/Juniper/asf/pkg/services"
)

func TestFieldMaskPaths(t *testing.T) {
	tests := []struct {
		name               string
		structure          *basedb.Structure
		path               string
		expectedChildPaths []string
		expectedLength     int
	}{
		{
			name:               "top level simple property",
			structure:          FirewallRuleStructure,
			path:               "uuid",
			expectedChildPaths: nil,
			expectedLength:     0,
		},
		{
			name:               "nested simple property",
			structure:          FirewallRuleStructure,
			path:               "match_tags.tag_list",
			expectedChildPaths: nil,
			expectedLength:     0,
		},
		{
			name:      "top level complex property",
			structure: FirewallRuleStructure,
			path:      "action_list",
			expectedChildPaths: []string{
				"action_list.gateway_name",
				"action_list.mirror_to.analyzer_ip_address",
			},
			expectedLength: 20,
		},
		{
			name:      "nested complex property",
			structure: FirewallRuleStructure,
			path:      "service.src_ports",
			expectedChildPaths: []string{
				"service.src_ports.start_port",
				"service.src_ports.end_port",
			},
			expectedLength: 2,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			paths := tt.structure.GetInnerPaths(tt.path)
			for _, p := range tt.expectedChildPaths {
				assert.Contains(t, paths, p)
			}
			logrus.Println(paths)
			assert.Equal(t, tt.expectedLength, len(paths))
		})
	}
}

func TestUpdateComplexField(t *testing.T) {
	tests := []struct {
		name           string
		resource       *models.FirewallRule
		updateResource *models.FirewallRule
		assertion      func(t *testing.T, expected, actual *models.FirewallRule)
		paths          []string
	}{
		{
			name: "zero simple property",
			resource: &models.FirewallRule{
				UUID: "hoge",
				Name: "hogehoge",
			},
			updateResource: &models.FirewallRule{
				UUID: "hoge",
			},
			paths: []string{
				"name",
			},
			assertion: func(t *testing.T, expected, actual *models.FirewallRule) {
				assert.Equal(t, expected.Name, actual.Name)
			},
		},
		{
			name: "zero complex property",
			resource: &models.FirewallRule{
				UUID: "hoge",
				IDPerms: &models.IdPermsType{
					Enable: true,
				},
			},
			updateResource: &models.FirewallRule{
				UUID: "hoge",
			},
			paths: []string{
				"id_perms",
			},
			assertion: func(t *testing.T, expected, actual *models.FirewallRule) {
				assert.Equal(t, false, actual.IDPerms.Enable)
			},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, err := db.CreateFirewallRule(context.Background(), createFireWallRuleRequest(tt.resource))
			require.NoError(t, err)
			defer func() {
				db.DeleteFirewallRule(context.Background(), &services.DeleteFirewallRuleRequest{ // nolint: errcheck
					ID: tt.resource.GetUUID(),
				})
			}()
			_, err = db.UpdateFirewallRule(context.Background(), updateFireWallRuleRequest(tt.updateResource, tt.paths))
			require.NoError(t, err)
			response, err := db.GetFirewallRule(context.Background(), &services.GetFirewallRuleRequest{
				ID: tt.resource.GetUUID(),
			})
			require.NoError(t, err)
			if tt.assertion != nil {
				tt.assertion(t, tt.updateResource, response.GetFirewallRule())
			}
		})
	}
}

func createFireWallRuleRequest(m *models.FirewallRule) *services.CreateFirewallRuleRequest {
	return &services.CreateFirewallRuleRequest{
		FirewallRule: m,
	}
}

func updateFireWallRuleRequest(m *models.FirewallRule, paths []string) *services.UpdateFirewallRuleRequest {
	return &services.UpdateFirewallRuleRequest{
		FirewallRule: m,
		FieldMask: types.FieldMask{
			Paths: paths,
		},
	}
}

func TestDBScanRow(t *testing.T) {
	tests := []struct {
		name       string
		schemaID   string
		row        map[string]interface{}
		fails      bool
		expected   proto.Message
		expectedFM *types.FieldMask
	}{
		{name: "nil", fails: true},
		{
			name:     "nil with valid schemaID",
			schemaID: "logical_interface",
			expected: (*models.LogicalInterface)(nil),
		},
		{name: "empty", row: map[string]interface{}{}, fails: true},
		{
			name:       "empty with valid schemaID",
			row:        map[string]interface{}{},
			schemaID:   "logical_interface",
			expected:   models.MakeLogicalInterface(),
			expectedFM: &types.FieldMask{},
		},
		{name: "valid logical_interface_row", schemaID: "logical_interface",
			row: map[string]interface{}{
				"configuration_version":      1,
				"created":                    "test created",
				"creator":                    "test creator",
				"description":                "test description",
				"display_name":               "test display name",
				"enable":                     true,
				"fq_name":                    []byte(`["first", "second"]`),
				"global_access":              2,
				"group":                      "test group",
				"group_access":               3,
				"key_value_pair":             []byte(`[{"key": "some key", "value": "some value"}]`),
				"last_modified":              "test last modified",
				"logical_interface_type":     "test type",
				"logical_interface_vlan_tag": 4,
				"other_access":               5,
				"owner":                      "test owner",
				"owner_access":               6,
				"parent_type":                "test parent type",
				"parent_uuid":                "test parent uuid",
				"permissions_owner":          "test perms owner",
				"permissions_owner_access":   7,
				"share":                      []byte(`[{"tenant_access": 1337, "tenant": "leet"}]`),
				"user_visible":               true,
				"uuid":                       "test uuid",
				"uuid_lslong":                8,
				"uuid_mslong":                9,
			},
			expected: &models.LogicalInterface{
				UUID:       "test uuid",
				ParentUUID: "test parent uuid",
				ParentType: "test parent type",
				FQName:     []string{"first", "second"},
				IDPerms: &models.IdPermsType{
					Enable:       true,
					Description:  "test description",
					Created:      "test created",
					Creator:      "test creator",
					UserVisible:  true,
					LastModified: "test last modified",
					Permissions: &models.PermType{
						Owner:       "test perms owner",
						OwnerAccess: 7,
						OtherAccess: 5,
						Group:       "test group",
						GroupAccess: 3,
					},
					UUID: &models.UuidType{
						UUIDMslong: 9,
						UUIDLslong: 8,
					},
				},
				DisplayName: "test display name",
				Annotations: &models.KeyValuePairs{
					KeyValuePair: []*models.KeyValuePair{{Value: "some value", Key: "some key"}},
				},
				Perms2: &models.PermType2{
					Owner:        "test owner",
					OwnerAccess:  6,
					GlobalAccess: 2,
					Share:        []*models.ShareType{{TenantAccess: 1337, Tenant: "leet"}},
				},
				ConfigurationVersion:        1,
				LogicalInterfaceVlanTag:     4,
				LogicalInterfaceType:        "test type",
				VirtualMachineInterfaceRefs: nil,
			},
			expectedFM: &types.FieldMask{Paths: []string{
				"uuid", "perms2.share", "perms2.owner_access", "perms2.owner",
				"perms2.global_access", "parent_uuid", "parent_type", "logical_interface_vlan_tag",
				"logical_interface_type", "id_perms.uuid.uuid_mslong", "id_perms.uuid.uuid_lslong",
				"id_perms.user_visible", "id_perms.permissions.owner_access", "id_perms.permissions.owner",
				"id_perms.permissions.other_access", "id_perms.permissions.group_access",
				"id_perms.permissions.group", "id_perms.last_modified", "id_perms.enable",
				"id_perms.description", "id_perms.creator", "id_perms.created", "fq_name",
				"display_name", "configuration_version", "annotations.key_value_pair",
			}},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			result, fm, err := db.ScanRow(tt.schemaID, tt.row)
			if tt.fails {
				assert.Error(t, err)
			} else {
				assert.NoError(t, err)
				assert.Equal(t, tt.expected, result)
				assert.Equal(t, tt.expectedFM, fm)
				if tt.expectedFM != nil {
					assert.Equal(t, len(tt.row), len(tt.expectedFM.Paths),
						"FieldMask should contain same number of paths as row does")
				}
			}
		})
	}
}

var exampleVN = &models.VirtualNetwork{
	UUID:       "vn_uuid",
	ParentType: "project",
	ParentUUID: "beefbeef-beef-beef-beef-beefbeef0003",
	FQName:     []string{"default-domain", "default-project", "vn-db-create-ref"},
}

var exampleRI = &models.RoutingInstance{
	UUID:       "ri_uuid",
	ParentType: "virtual-network",
	ParentUUID: "vn_uuid",
	FQName:     []string{"default-domain", "default-project", "vn-db-create-ref", "ri-db-create-ref"},
}

var exampleRT = &models.RouteTarget{
	UUID:   "rt_uuid",
	FQName: []string{"default-domain", "default-project", "vn-db-create-ref", "rt-db-create-ref"},
}

func TestDBCreateRef(t *testing.T) {
	vnUUID, riUUID, rtUUID := exampleVN.UUID, exampleRI.UUID, exampleRT.UUID

	tests := []struct {
		name     string
		request  services.CreateRoutingInstanceRouteTargetRefRequest
		fails    bool
		expected *services.CreateRoutingInstanceRouteTargetRefResponse
	}{
		{name: "empty", fails: true},
		{
			name: "objects missing",
			request: services.CreateRoutingInstanceRouteTargetRefRequest{
				ID:                            "foo",
				RoutingInstanceRouteTargetRef: &models.RoutingInstanceRouteTargetRef{UUID: "bar"},
			},
			fails: true,
		},
		{
			name: "valid ID invalid ref UUID",
			request: services.CreateRoutingInstanceRouteTargetRefRequest{
				ID:                            riUUID,
				RoutingInstanceRouteTargetRef: &models.RoutingInstanceRouteTargetRef{UUID: "bar"},
			},
			fails: true,
		},
		{
			name: "valid ID valid ref UUID",
			request: services.CreateRoutingInstanceRouteTargetRefRequest{
				ID: riUUID,
				RoutingInstanceRouteTargetRef: &models.RoutingInstanceRouteTargetRef{
					UUID: rtUUID,
					To:   []string{"default-domain", "default-project", "vn-db-create-ref", "ri-db-create-ref"},
				},
			},
			expected: &services.CreateRoutingInstanceRouteTargetRefResponse{
				ID: riUUID,
				RoutingInstanceRouteTargetRef: &models.RoutingInstanceRouteTargetRef{
					UUID: rtUUID,
					To:   []string{"default-domain", "default-project", "vn-db-create-ref", "ri-db-create-ref"},
				},
			},
		},
		{
			name: "valid ID valid ref UUID with attrs",
			request: services.CreateRoutingInstanceRouteTargetRefRequest{
				ID: riUUID,
				RoutingInstanceRouteTargetRef: &models.RoutingInstanceRouteTargetRef{
					UUID: rtUUID,
					Attr: &models.InstanceTargetType{ImportExport: "import:export"},
					To:   []string{"default-domain", "default-project", "vn-db-create-ref", "ri-db-create-ref"},
				},
			},
			expected: &services.CreateRoutingInstanceRouteTargetRefResponse{
				ID: riUUID,
				RoutingInstanceRouteTargetRef: &models.RoutingInstanceRouteTargetRef{
					UUID: rtUUID,
					Attr: &models.InstanceTargetType{ImportExport: "import:export"},
					To:   []string{"default-domain", "default-project", "vn-db-create-ref", "ri-db-create-ref"},
				},
			},
		},
	}

	setup := func(t *testing.T) {
		ctx := context.Background()
		_, err := db.CreateVirtualNetwork(ctx, &services.CreateVirtualNetworkRequest{VirtualNetwork: exampleVN})
		require.NoError(t, err)
		_, err = db.CreateRouteTarget(ctx, &services.CreateRouteTargetRequest{RouteTarget: exampleRT})
		require.NoError(t, err)
		_, err = db.CreateRoutingInstance(ctx, &services.CreateRoutingInstanceRequest{RoutingInstance: exampleRI})
		require.NoError(t, err)
	}
	teardown := func(t *testing.T) {
		ctx := context.Background()
		_, err := db.DeleteRoutingInstance(ctx, &services.DeleteRoutingInstanceRequest{ID: riUUID})
		assert.NoError(t, err)
		_, err = db.DeleteRouteTarget(ctx, &services.DeleteRouteTargetRequest{ID: rtUUID})
		assert.NoError(t, err)
		_, err = db.DeleteVirtualNetwork(ctx, &services.DeleteVirtualNetworkRequest{ID: vnUUID})
		assert.NoError(t, err)
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			defer teardown(t)
			setup(t)

			response, err := db.CreateRoutingInstanceRouteTargetRef(context.Background(), &tt.request)
			assert.Equal(t, tt.expected, response)

			if tt.fails {
				assert.Error(t, err)
			} else {
				assert.NoError(t, err)

				getResp, err := db.GetRoutingInstance(context.Background(), &services.GetRoutingInstanceRequest{ID: riUUID})
				assert.NoError(t, err)

				assert.Len(t, getResp.RoutingInstance.RouteTargetRefs, 1)
			}
		})
	}
}

func TestDBDeleteRef(t *testing.T) {
	vnUUID, riUUID, rtUUID := exampleVN.UUID, exampleRI.UUID, exampleRT.UUID

	tests := []struct { // nolint: maligned
		name           string
		request        services.DeleteRoutingInstanceRouteTargetRefRequest
		fails          bool
		expected       *services.DeleteRoutingInstanceRouteTargetRefResponse
		shouldRefExist bool
	}{
		{name: "empty", fails: true},
		{
			name: "objects missing",
			request: services.DeleteRoutingInstanceRouteTargetRefRequest{
				ID:                            "foo",
				RoutingInstanceRouteTargetRef: &models.RoutingInstanceRouteTargetRef{UUID: "bar"},
			},
			expected: &services.DeleteRoutingInstanceRouteTargetRefResponse{
				ID:                            "foo",
				RoutingInstanceRouteTargetRef: &models.RoutingInstanceRouteTargetRef{UUID: "bar"},
			},
			shouldRefExist: true,
		},
		{
			name: "valid ID invalid ref UUID",
			request: services.DeleteRoutingInstanceRouteTargetRefRequest{
				ID:                            riUUID,
				RoutingInstanceRouteTargetRef: &models.RoutingInstanceRouteTargetRef{UUID: "bar"},
			},
			expected: &services.DeleteRoutingInstanceRouteTargetRefResponse{
				ID:                            riUUID,
				RoutingInstanceRouteTargetRef: &models.RoutingInstanceRouteTargetRef{UUID: "bar"},
			},
			shouldRefExist: true,
		},
		{
			name: "valid ID valid ref UUID",
			request: services.DeleteRoutingInstanceRouteTargetRefRequest{
				ID:                            riUUID,
				RoutingInstanceRouteTargetRef: &models.RoutingInstanceRouteTargetRef{UUID: rtUUID},
			},
			expected: &services.DeleteRoutingInstanceRouteTargetRefResponse{
				ID:                            riUUID,
				RoutingInstanceRouteTargetRef: &models.RoutingInstanceRouteTargetRef{UUID: rtUUID},
			},
			shouldRefExist: false,
		},
	}

	setup := func(t *testing.T) {
		ctx := context.Background()
		_, err := db.CreateVirtualNetwork(ctx, &services.CreateVirtualNetworkRequest{VirtualNetwork: exampleVN})
		require.NoError(t, err)
		_, err = db.CreateRouteTarget(ctx, &services.CreateRouteTargetRequest{RouteTarget: exampleRT})
		require.NoError(t, err)

		// create routing instance with ref to route target
		ri := *exampleRI
		ri.RouteTargetRefs = []*models.RoutingInstanceRouteTargetRef{{UUID: rtUUID}}
		_, err = db.CreateRoutingInstance(ctx, &services.CreateRoutingInstanceRequest{RoutingInstance: &ri})
		require.NoError(t, err)
	}
	teardown := func(t *testing.T) {
		ctx := context.Background()
		_, err := db.DeleteRoutingInstance(ctx, &services.DeleteRoutingInstanceRequest{ID: riUUID})
		assert.NoError(t, err)
		_, err = db.DeleteRouteTarget(ctx, &services.DeleteRouteTargetRequest{ID: rtUUID})
		assert.NoError(t, err)
		_, err = db.DeleteVirtualNetwork(ctx, &services.DeleteVirtualNetworkRequest{ID: vnUUID})
		assert.NoError(t, err)
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			defer teardown(t)
			setup(t)

			response, err := db.DeleteRoutingInstanceRouteTargetRef(context.Background(), &tt.request)
			assert.Equal(t, tt.expected, response)

			if tt.fails {
				assert.Error(t, err)
			} else {
				assert.NoError(t, err)

				getResp, err := db.GetRoutingInstance(context.Background(), &services.GetRoutingInstanceRequest{ID: riUUID})
				assert.NoError(t, err)

				if tt.shouldRefExist {
					assert.Len(t, getResp.RoutingInstance.RouteTargetRefs, 1)
				} else {
					assert.Len(t, getResp.RoutingInstance.RouteTargetRefs, 0)
				}
			}
		})
	}
}
