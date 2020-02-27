package models

import (
	"github.com/pkg/errors"
)

// GetNetworkIpamRefUUIDs get UUIDs of IPAM references.
func (m *VirtualRouter) GetNetworkIpamRefUUIDs() (uuids []string) {
	for _, ref := range m.GetNetworkIpamRefs() {
		uuids = append(uuids, ref.GetUUID())
	}
	return uuids
}

// GetRefUUIDToNetworkIpamRefMap get UUID to IPAM references map.
func (m *VirtualRouter) GetRefUUIDToNetworkIpamRefMap() map[string]*VirtualRouterNetworkIpamRef {
	uuidToRefMap := make(map[string]*VirtualRouterNetworkIpamRef)
	for _, ref := range m.GetNetworkIpamRefs() {
		uuidToRefMap[ref.GetUUID()] = ref
	}
	return uuidToRefMap
}

// GetAllocationPools gets allocation pools.
func (m *VirtualRouter) GetAllocationPools() (allocationPools []*AllocationPoolType) {
	for _, ipamRef := range m.GetNetworkIpamRefs() {
		allocationPools = append(allocationPools, ipamRef.GetAttr().GetAllocationPools()...)
	}

	return allocationPools
}

// GetVrouterSpecificAllocationPools gets vrouter specific allocation pools.
func (ref *VirtualRouterNetworkIpamRef) GetVrouterSpecificAllocationPools() (vrSpecificPools []*AllocationPoolType) {
	for _, vrAllocPool := range ref.GetAttr().GetAllocationPools() {
		if !vrAllocPool.GetVrouterSpecificPool() {
			continue
		}
		vrSpecificPools = append(vrSpecificPools, vrAllocPool)
	}

	return vrSpecificPools
}

// Validate checks if reference data are correct.
func (ref VirtualRouterNetworkIpamRef) Validate() error {
	if len(ref.GetAttr().GetAllocationPools()) == 0 {
		return errors.Errorf("no allocation-pools for this vrouter")
	}

	vrSubnets := ref.GetAttr().GetSubnet()
	for _, vrSubnet := range vrSubnets {
		if err := vrSubnet.Validate(); err != nil {
			return errors.Errorf("couldn't validate vrouter subnet: %v", err)
		}
	}

	return nil
}

// ValidateLinkToIpam checks if alloc-pools are configured correctly.
func (ref VirtualRouterNetworkIpamRef) ValidateLinkToIpam(netIpam *NetworkIpam) error {
	// read data on the link between vrouter and ipam
	// if alloc pool exists, then make sure that alloc-pools are
	// configured in ipam subnet with a flag indicating
	// vrouter specific allocation pool

	vrAllocPools := ref.GetVrouterSpecificAllocationPools()
	for _, vrAllocPool := range vrAllocPools {
		if !netIpam.ContainsAllocationPool(vrAllocPool) {
			return errors.Errorf("vrouter allocation-pool start:%s, end %s not in ipam %s",
				vrAllocPool.GetStart(), vrAllocPool.GetEnd(), netIpam.GetUUID())
		}
	}

	return nil
}
