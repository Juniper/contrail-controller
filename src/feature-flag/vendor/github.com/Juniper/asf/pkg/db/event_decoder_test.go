package db

import (
	"testing"

	"github.com/gogo/protobuf/types"
	"github.com/stretchr/testify/assert"

	"github.com/Juniper/asf/pkg/models"
	"github.com/Juniper/asf/pkg/models/basemodels"
	"github.com/Juniper/asf/pkg/services"
)

func TestDecodeRowEvent(t *testing.T) {
	tests := []struct {
		name                    string
		operation, resourceName string
		pk                      []string
		properties              map[string]interface{}
		expected                *services.Event
		fails                   bool
	}{
		{name: "nil", fails: true},
		{name: "resourceName only", resourceName: "virtual_network", fails: true},
		{
			name:         "primary key with two elements",
			resourceName: "virtual_network",
			pk:           []string{"multiple", "keys"},
			fails:        true,
		},
		{
			name:         "empty resource (operation defaults to create)",
			resourceName: "virtual_network",
			pk:           []string{"some-uuid"},
			expected: &services.Event{
				Request: &services.Event_CreateVirtualNetworkRequest{
					CreateVirtualNetworkRequest: &services.CreateVirtualNetworkRequest{
						VirtualNetwork: &models.VirtualNetwork{},
					},
				},
			},
		},
		{
			name:         "update empty resource",
			resourceName: "virtual_network",
			operation:    services.OperationUpdate,
			pk:           []string{"some-uuid"},
			expected: &services.Event{
				Request: &services.Event_UpdateVirtualNetworkRequest{
					UpdateVirtualNetworkRequest: &services.UpdateVirtualNetworkRequest{
						VirtualNetwork: &models.VirtualNetwork{UUID: "some-uuid"},
					},
				},
			},
		},
		{
			name:         "delete empty resource",
			resourceName: "virtual_network",
			operation:    services.OperationDelete,
			pk:           []string{"some-uuid"},
			expected: &services.Event{
				Request: &services.Event_DeleteVirtualNetworkRequest{
					DeleteVirtualNetworkRequest: &services.DeleteVirtualNetworkRequest{
						ID: "some-uuid",
					},
				},
			},
		},
		{
			name:         "create vn",
			resourceName: "virtual_network",
			operation:    services.OperationCreate,
			pk:           []string{"some-uuid"},
			properties: map[string]interface{}{
				"uuid":                       "some-uuid",
				"virtual_network_network_id": 1337,
				"creator":                    "admin",
			},
			expected: &services.Event{
				Request: &services.Event_CreateVirtualNetworkRequest{
					CreateVirtualNetworkRequest: &services.CreateVirtualNetworkRequest{
						VirtualNetwork: &models.VirtualNetwork{
							UUID:                    "some-uuid",
							VirtualNetworkNetworkID: 1337,
							IDPerms:                 &models.IdPermsType{Creator: "admin"},
						},
						FieldMask: types.FieldMask{Paths: []string{
							models.VirtualNetworkFieldVirtualNetworkNetworkID,
							models.VirtualNetworkFieldUUID,
							basemodels.JoinPath(
								models.VirtualNetworkFieldIDPerms,
								models.IdPermsTypeFieldCreator,
							),
						}},
					},
				},
			},
		},
		{
			name:         "update vn",
			resourceName: "virtual_network",
			operation:    services.OperationUpdate,
			pk:           []string{"some-uuid"},
			properties: map[string]interface{}{
				"virtual_network_network_id": 1337,
				"creator":                    "admin",
			},
			expected: &services.Event{
				Request: &services.Event_UpdateVirtualNetworkRequest{
					UpdateVirtualNetworkRequest: &services.UpdateVirtualNetworkRequest{
						VirtualNetwork: &models.VirtualNetwork{
							UUID:                    "some-uuid",
							VirtualNetworkNetworkID: 1337,
							IDPerms:                 &models.IdPermsType{Creator: "admin"},
						},
						FieldMask: types.FieldMask{Paths: []string{
							models.VirtualNetworkFieldVirtualNetworkNetworkID,
							basemodels.JoinPath(
								models.VirtualNetworkFieldIDPerms,
								models.IdPermsTypeFieldCreator,
							),
						}},
					},
				},
			},
		},
		{
			name:         "create ref",
			resourceName: "ref_virtual_network_network_ipam",
			operation:    services.OperationCreate,
			pk:           []string{"vn-uuid", "ipam-uuid"},
			properties: map[string]interface{}{
				"route": []*models.RouteType{{Prefix: "10"}},
			},
			expected: &services.Event{
				Request: &services.Event_CreateVirtualNetworkNetworkIpamRefRequest{
					CreateVirtualNetworkNetworkIpamRefRequest: &services.CreateVirtualNetworkNetworkIpamRefRequest{
						ID: "vn-uuid",
						VirtualNetworkNetworkIpamRef: &models.VirtualNetworkNetworkIpamRef{
							UUID: "ipam-uuid",
							Attr: &models.VnSubnetsType{
								HostRoutes: &models.RouteTableType{
									Route: []*models.RouteType{{Prefix: "10"}},
								},
								IpamSubnets: []*models.IpamSubnetType{},
							},
						},
					},
				},
			},
		},
		{
			name:         "delete ref",
			resourceName: "ref_virtual_network_network_ipam",
			operation:    services.OperationDelete,
			pk:           []string{"vn-uuid", "ipam-uuid"},
			properties:   map[string]interface{}{},
			expected: &services.Event{
				Request: &services.Event_DeleteVirtualNetworkNetworkIpamRefRequest{
					DeleteVirtualNetworkNetworkIpamRefRequest: &services.DeleteVirtualNetworkNetworkIpamRefRequest{
						ID: "vn-uuid",
						VirtualNetworkNetworkIpamRef: &models.VirtualNetworkNetworkIpamRef{
							UUID: "ipam-uuid",
						},
					},
				},
			},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			ev, err := db.DecodeRowEvent(tt.operation, tt.resourceName, tt.pk, tt.properties)
			if tt.fails {
				assert.Error(t, err)
			} else {
				assert.NoError(t, err)
			}

			assert.Equal(t, tt.expected, ev)
		})
	}
}
