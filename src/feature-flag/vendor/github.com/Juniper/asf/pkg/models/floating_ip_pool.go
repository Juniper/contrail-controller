package models

import (
	"github.com/Juniper/asf/pkg/errutil"
)

// IsParentTypeVirtualNetwork checks if parent's type is virtual network
func (fipp *FloatingIPPool) IsParentTypeVirtualNetwork() bool {
	var m VirtualNetwork
	return fipp.GetParentType() == m.Kind()
}

// HasSubnets checks if floating-ip-pool has any subnets defined
func (fipp *FloatingIPPool) HasSubnets() bool {
	floatingIPPoolSubnets := fipp.GetFloatingIPPoolSubnets()
	return floatingIPPoolSubnets != nil && len(floatingIPPoolSubnets.GetSubnetUUID()) != 0
}

// CheckAreSubnetsInVirtualNetworkSubnets checks if subnets defined in floating-ip-pool object
// are present in the virtual-network
func (fipp *FloatingIPPool) CheckAreSubnetsInVirtualNetworkSubnets(vn *VirtualNetwork) error {
	for _, floatingIPPoolSubnetUUID := range fipp.GetFloatingIPPoolSubnets().GetSubnetUUID() {
		subnetFound := false
		for _, ipam := range vn.GetNetworkIpamRefs() {
			for _, ipamSubnet := range ipam.GetAttr().GetIpamSubnets() {
				if ipamSubnet.GetSubnetUUID() == floatingIPPoolSubnetUUID {
					subnetFound = true
					break
				}
			}
		}

		if !subnetFound {
			return errutil.ErrorBadRequestf("Subnet %s was not found in virtual-network %s",
				floatingIPPoolSubnetUUID, vn.GetUUID())
		}
	}
	return nil
}

// GetIPsInSubnets returns list of floating ips which belong to provided ipam subnets
func (fipp *FloatingIPPool) GetIPsInSubnets(ipamSubnets *IpamSubnets) ([]string, error) {
	var ipsInSubnets []string
	for _, floatingIP := range fipp.GetFloatingIPs() {
		contains, err := ipamSubnets.Contains(floatingIP.GetFloatingIPAddress())
		if err != nil {
			return nil, err
		}
		if contains {
			ipsInSubnets = append(ipsInSubnets, floatingIP.GetFloatingIPAddress())
		}
	}

	return ipsInSubnets, nil
}

//GetFloatingIPsUUIDs returns list of floating ips' uuids associated with floating ip pool
func (fipp *FloatingIPPool) GetFloatingIPsUUIDs() (uuids []string) {
	for _, fip := range fipp.GetFloatingIPs() {
		uuids = append(uuids, fip.GetUUID())
	}
	return uuids
}
