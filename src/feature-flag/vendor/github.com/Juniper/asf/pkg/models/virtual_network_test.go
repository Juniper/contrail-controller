package models

import (
	"testing"

	"github.com/stretchr/testify/assert"

	"github.com/Juniper/asf/pkg/models/basemodels"
)

func TestCheckMultiPolicyServiceChainConfig(t *testing.T) {
	var tests = []struct {
		name           string
		virtualNetwork *VirtualNetwork
		fails          bool
	}{
		{
			name: "check for rt with different RT in import and export list",
			virtualNetwork: &VirtualNetwork{
				MultiPolicyServiceChainsEnabled: true,
				ImportRouteTargetList: &RouteTargetList{
					RouteTarget: []string{"100:101"},
				},
				ExportRouteTargetList: &RouteTargetList{
					RouteTarget: []string{"100:102"},
				},
			},
		},
		{
			name:  "check for rt with the same RT in both import and export list",
			fails: true,
			virtualNetwork: &VirtualNetwork{
				MultiPolicyServiceChainsEnabled: true,
				ImportRouteTargetList: &RouteTargetList{
					RouteTarget: []string{"100:101"},
				},
				ExportRouteTargetList: &RouteTargetList{
					RouteTarget: []string{"100:101"},
				},
			},
		},
		{
			name: "check for multi-policy service chains disabled",
			virtualNetwork: &VirtualNetwork{
				MultiPolicyServiceChainsEnabled: false,
			},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if tt.fails {
				assert.Error(t, tt.virtualNetwork.CheckMultiPolicyServiceChainConfig())
			} else {
				assert.NoError(t, tt.virtualNetwork.CheckMultiPolicyServiceChainConfig())
			}
		})
	}
}

func TestMakeNeutronCompatible(t *testing.T) {
	var tests = []struct {
		name           string
		virtualNetwork *VirtualNetwork
		expected       *VirtualNetwork
	}{
		{
			name: "check for is shared",
			virtualNetwork: &VirtualNetwork{
				IsShared: true,
				Perms2:   &PermType2{},
			},
			expected: &VirtualNetwork{
				IsShared: true,
				Perms2: &PermType2{
					GlobalAccess: basemodels.PermsRWX,
				},
			},
		},
		{
			name: "check for RWX global access",
			virtualNetwork: &VirtualNetwork{
				IsShared: false,
				Perms2: &PermType2{
					GlobalAccess: basemodels.PermsRWX,
				},
			},
			expected: &VirtualNetwork{
				IsShared: true,
				Perms2: &PermType2{
					GlobalAccess: basemodels.PermsRWX,
				},
			},
		},
		{
			name: "check for is shared and RWX global access ",
			virtualNetwork: &VirtualNetwork{
				IsShared: true,
				Perms2: &PermType2{
					GlobalAccess: basemodels.PermsRWX,
				},
			},
			expected: &VirtualNetwork{
				IsShared: true,
				Perms2: &PermType2{
					GlobalAccess: basemodels.PermsRWX,
				},
			},
		},
		{
			name: "check for is shared and not RWX global access ",
			virtualNetwork: &VirtualNetwork{
				IsShared: true,
				Perms2: &PermType2{
					GlobalAccess: basemodels.PermsW,
				},
			},
			expected: &VirtualNetwork{
				IsShared: true,
				Perms2: &PermType2{
					GlobalAccess: basemodels.PermsRWX,
				},
			},
		},
		{
			name: "check for basemodels.PermsW global access ",
			virtualNetwork: &VirtualNetwork{
				IsShared: false,
				Perms2: &PermType2{
					GlobalAccess: basemodels.PermsW,
				},
			},
			expected: &VirtualNetwork{
				IsShared: false,
				Perms2: &PermType2{
					GlobalAccess: basemodels.PermsW,
				},
			},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			tt.virtualNetwork.MakeNeutronCompatible()
			assert.Equal(t, tt.virtualNetwork, tt.expected)
		})
	}
}

var sampleVNNetworkIpamRefAttrs = &VnSubnetsType{HostRoutes: &RouteTableType{Route: []*RouteType{{Prefix: "prefix"}}}}

func TestVirtualNetworkAddNetworkIpamRef(t *testing.T) {
	var tests = []struct {
		name           string
		virtualNetwork VirtualNetwork
		toAdd          *VirtualNetworkNetworkIpamRef
		expected       VirtualNetwork
	}{
		{name: "empty"},
		{
			name:  "add new ref",
			toAdd: &VirtualNetworkNetworkIpamRef{UUID: "new-ref"},
			expected: VirtualNetwork{
				NetworkIpamRefs: []*VirtualNetworkNetworkIpamRef{{UUID: "new-ref"}},
			},
		},
		{
			name: "add new ref with attrs",
			toAdd: &VirtualNetworkNetworkIpamRef{
				UUID: "new-ref",
				Attr: sampleVNNetworkIpamRefAttrs,
			},
			expected: VirtualNetwork{
				NetworkIpamRefs: []*VirtualNetworkNetworkIpamRef{{
					UUID: "new-ref",
					Attr: sampleVNNetworkIpamRefAttrs,
				}},
			},
		},
		{
			name: "add new ref with old ref existing",
			virtualNetwork: VirtualNetwork{
				NetworkIpamRefs: []*VirtualNetworkNetworkIpamRef{{UUID: "old-ref"}},
			},
			toAdd: &VirtualNetworkNetworkIpamRef{UUID: "new-ref"},
			expected: VirtualNetwork{
				NetworkIpamRefs: []*VirtualNetworkNetworkIpamRef{{UUID: "old-ref"}, {UUID: "new-ref"}},
			},
		},
		{
			name: "update ref with same UUID",
			virtualNetwork: VirtualNetwork{
				NetworkIpamRefs: []*VirtualNetworkNetworkIpamRef{{UUID: "new-ref"}},
			},
			toAdd: &VirtualNetworkNetworkIpamRef{UUID: "new-ref"},
			expected: VirtualNetwork{
				NetworkIpamRefs: []*VirtualNetworkNetworkIpamRef{{UUID: "new-ref"}},
			},
		},
		{
			name: "update ref with same UUID and update attr",
			virtualNetwork: VirtualNetwork{
				NetworkIpamRefs: []*VirtualNetworkNetworkIpamRef{{UUID: "old-ref"}, {UUID: "new-ref"}},
			},
			toAdd: &VirtualNetworkNetworkIpamRef{
				UUID: "new-ref",
				Attr: sampleVNNetworkIpamRefAttrs,
			},
			expected: VirtualNetwork{
				NetworkIpamRefs: []*VirtualNetworkNetworkIpamRef{
					{UUID: "old-ref"},
					{UUID: "new-ref", Attr: sampleVNNetworkIpamRefAttrs},
				},
			},
		},
		{
			name: "remove attr by updating ref",
			virtualNetwork: VirtualNetwork{
				NetworkIpamRefs: []*VirtualNetworkNetworkIpamRef{{
					UUID: "new-ref",
					Attr: sampleVNNetworkIpamRefAttrs,
				}},
			},
			toAdd: &VirtualNetworkNetworkIpamRef{UUID: "new-ref"},
			expected: VirtualNetwork{
				NetworkIpamRefs: []*VirtualNetworkNetworkIpamRef{{UUID: "new-ref"}},
			},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			tt.virtualNetwork.AddNetworkIpamRef(tt.toAdd)
			assert.Equal(t, tt.expected.NetworkIpamRefs, tt.virtualNetwork.NetworkIpamRefs)
			assert.Equal(t, tt.expected, tt.virtualNetwork)
		})
	}
}

func TestVirtualNetworkRemoveNetworkIpamRef(t *testing.T) {
	var tests = []struct {
		name           string
		virtualNetwork VirtualNetwork
		toRemove       *VirtualNetworkNetworkIpamRef
		expected       VirtualNetwork
	}{
		{name: "empty"},
		{
			name: "delete ref",
			virtualNetwork: VirtualNetwork{
				NetworkIpamRefs: []*VirtualNetworkNetworkIpamRef{{UUID: "to-delete-ref"}},
			},
			toRemove: &VirtualNetworkNetworkIpamRef{UUID: "to-delete-ref"},
			expected: VirtualNetwork{
				NetworkIpamRefs: []*VirtualNetworkNetworkIpamRef{},
			},
		},
		{
			name: "try to delete non existing ref",
			virtualNetwork: VirtualNetwork{
				NetworkIpamRefs: []*VirtualNetworkNetworkIpamRef{{UUID: "still-alive"}},
			},
			toRemove: &VirtualNetworkNetworkIpamRef{UUID: "to-delete-ref"},
			expected: VirtualNetwork{
				NetworkIpamRefs: []*VirtualNetworkNetworkIpamRef{{UUID: "still-alive"}},
			},
		},
		{
			name: "delete ref when multiple exist",
			virtualNetwork: VirtualNetwork{
				NetworkIpamRefs: []*VirtualNetworkNetworkIpamRef{{UUID: "still-alive"}, {UUID: "to-delete-ref"}},
			},
			toRemove: &VirtualNetworkNetworkIpamRef{UUID: "to-delete-ref"},
			expected: VirtualNetwork{
				NetworkIpamRefs: []*VirtualNetworkNetworkIpamRef{{UUID: "still-alive"}},
			},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			tt.virtualNetwork.RemoveNetworkIpamRef(tt.toRemove)
			assert.Equal(t, tt.expected.NetworkIpamRefs, tt.virtualNetwork.NetworkIpamRefs)
			assert.Equal(t, tt.expected, tt.virtualNetwork)
		})
	}
}

func TestVirtualNetworkMakeDefaultRoutingInstance(t *testing.T) {
	tests := []struct {
		name           string
		virtualNetwork *VirtualNetwork
		expected       *RoutingInstance
	}{
		{
			name: "FabricSnat enabled",
			virtualNetwork: &VirtualNetwork{
				Name: "test-network",
				FQName: []string{
					"domain",
					"project",
					"test-network"},
				UUID:       "test-network-uuid",
				FabricSnat: true,
			},
			expected: &RoutingInstance{
				Name: "test-network",
				FQName: []string{
					"domain",
					"project",
					"test-network",
					"test-network"},
				ParentUUID:                "test-network-uuid",
				RoutingInstanceIsDefault:  true,
				RoutingInstanceFabricSnat: true,
			},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			actual := tt.virtualNetwork.MakeDefaultRoutingInstance()
			assert.Equal(t, tt.expected, actual)
		})
	}
}
