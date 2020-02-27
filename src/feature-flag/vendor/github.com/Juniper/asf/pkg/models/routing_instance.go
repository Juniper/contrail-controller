package models

import "github.com/Juniper/asf/pkg/models/basemodels"

// IsIPFabric returns true if routing instance FQName fits IP Fabric
func (ri *RoutingInstance) IsIPFabric() bool {
	fq := []string{"default-domain", "default-project", "ip-fabric", "__default__"}
	return basemodels.FQNameEquals(fq, ri.GetFQName())
}

// IsLinkLocal returns true if routing instance FQName fits Link Local
func (ri *RoutingInstance) IsLinkLocal() bool {
	fq := []string{"default-domain", "default-project", "__link_local__", "__link_local__"}
	return basemodels.FQNameEquals(fq, ri.GetFQName())
}
