package models

import (
	"fmt"

	"github.com/Juniper/asf/pkg/errutil"
	"github.com/Juniper/asf/pkg/format"
	"github.com/Juniper/asf/pkg/models/basemodels"
)

// Virtual network forwarding modes.
const (
	L3Mode   = "l3"
	L2L3Mode = "l2_l3"
)

// TODO: Enums strings should be generated from schema
const (
	UserDefinedSubnetOnly      = "user-defined-subnet-only"
	UserDefinedSubnetPreferred = "user-defined-subnet-preferred"
	FlatSubnetOnly             = "flat-subnet-only"
)

//MakeNeutronCompatible makes this resource data neutron compatible.
func (m *VirtualNetwork) MakeNeutronCompatible() {
	//  neutorn <-> vnc sharing
	if m.Perms2.GlobalAccess == basemodels.PermsRWX {
		m.IsShared = true
	}
	if m.IsShared {
		m.Perms2.GlobalAccess = basemodels.PermsRWX
	}
}

//HasVirtualNetworkNetworkID check if the resource has virtual network ID.
func (m *VirtualNetwork) HasVirtualNetworkNetworkID() bool {
	return m.VirtualNetworkNetworkID != 0
}

//IsSupportingAnyVPNType checks if this network is l2 and l3 mode
func (m *VirtualNetwork) IsSupportingAnyVPNType() bool {
	return m.GetVirtualNetworkProperties().GetForwardingMode() == L2L3Mode
}

//IsSupportingL3VPNType checks if this network is l3 mode
func (m *VirtualNetwork) IsSupportingL3VPNType() bool {
	return m.GetVirtualNetworkProperties().GetForwardingMode() == L3Mode
}

// CheckMultiPolicyServiceChainConfig checks if multi policy service chain config is valid.
func (m *VirtualNetwork) CheckMultiPolicyServiceChainConfig() error {
	if !m.MultiPolicyServiceChainsEnabled {
		return nil
	}

	if len(m.GetRouteTargetList().GetRouteTarget()) != 0 {
		return errutil.ErrorBadRequest("Multi Policy Service Chains enabled: " +
			"Route Target List should be empty")
	}

	if m.isAnyRouteTargetInImportAndExportList() {
		return errutil.ErrorBadRequest("Multi Policy Service Chains enabled: " +
			"there cannot be same Route Target in Import Route Target List and Export Route Target List")
	}

	return nil
}

func (m *VirtualNetwork) isAnyRouteTargetInImportAndExportList() bool {
	importRTs := make(map[string]bool)
	for _, importRT := range m.GetImportRouteTargetList().GetRouteTarget() {
		importRTs[importRT] = true
	}

	for _, exportRT := range m.GetExportRouteTargetList().GetRouteTarget() {
		if importRTs[exportRT] {
			return true
		}
	}

	return false
}

//ShouldIgnoreAllocation checks if there is ip-fabric or link-local address allocation
func (m *VirtualNetwork) ShouldIgnoreAllocation() bool {
	fqName := m.GetFQName()
	if format.ContainsString(fqName, "ip-fabric") || format.ContainsString(fqName, "__link_local__") {
		return true
	}
	return false
}

// GetIpamSubnets returns list of subnets contained in IPAM references of this VN.
func (m *VirtualNetwork) GetIpamSubnets() (s *IpamSubnets) {
	s = &IpamSubnets{}
	for _, networkIpam := range m.GetNetworkIpamRefs() {
		s.Subnets = append(s.Subnets, networkIpam.GetAttr().GetIpamSubnets()...)
	}
	return s
}

// GetAddressAllocationMethod returns address allocation method
func (m *VirtualNetwork) GetAddressAllocationMethod() string {
	allocationMethod := UserDefinedSubnetPreferred
	if m.GetAddressAllocationMode() != "" {
		allocationMethod = m.GetAddressAllocationMode()
	}
	return allocationMethod
}

// GetDefaultRoutingInstance returns the default routing instance of VN or nil if it doesn't exist
func (m *VirtualNetwork) GetDefaultRoutingInstance() *RoutingInstance {
	for _, ri := range m.RoutingInstances {
		if ri.GetRoutingInstanceIsDefault() {
			return ri
		}
	}

	return nil
}

// HasNetworkBasedAllocationMethod checks if allocation method is userdefined or flat subnet only
func (m *VirtualNetwork) HasNetworkBasedAllocationMethod() bool {
	return m.GetAddressAllocationMethod() == UserDefinedSubnetOnly || m.GetAddressAllocationMethod() == FlatSubnetOnly
}

// MakeDefaultRoutingInstance returns the default routing instance for the network.
func (m *VirtualNetwork) MakeDefaultRoutingInstance() *RoutingInstance {
	return &RoutingInstance{
		Name:                      m.Name,
		FQName:                    m.DefaultRoutingInstanceFQName(),
		ParentUUID:                m.UUID,
		RoutingInstanceIsDefault:  true,
		RoutingInstanceFabricSnat: m.FabricSnat,
	}
}

// DefaultRoutingInstanceFQName returns the FQName of the network's default RoutingInstance.
func (m *VirtualNetwork) DefaultRoutingInstanceFQName() []string {
	return basemodels.ChildFQName(m.FQName, m.FQName[len(m.FQName)-1])
}

// IsLinkLocal returns true if virtual network FQName fits Link Local
func (m *VirtualNetwork) IsLinkLocal() bool {
	fq := []string{"default-domain", "default-project", "__link_local__"}
	return basemodels.FQNameEquals(fq, m.GetFQName())
}

// VxLANIntOwner returns the owner for allocating the network's VxLAN IDs.
func (m *VirtualNetwork) VxLANIntOwner() string {
	return fmt.Sprintf("%s_vxlan", basemodels.FQNameToString(m.GetFQName()))
}
