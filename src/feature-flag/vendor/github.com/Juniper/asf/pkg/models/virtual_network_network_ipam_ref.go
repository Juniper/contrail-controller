package models

// RemoveSubnet removes IpamSubnetType with specified id from IpamSubnets.
func (m *VirtualNetworkNetworkIpamRef) RemoveSubnet(id string) {
	m.Attr.IpamSubnets = filterIpamSubnets(m.Attr.IpamSubnets, func(s *IpamSubnetType) bool {
		return s.SubnetUUID != id
	})
}

func filterIpamSubnets(subnets []*IpamSubnetType, predicate func(*IpamSubnetType) bool) []*IpamSubnetType {
	var filtered []*IpamSubnetType
	for _, s := range subnets {
		if predicate(s) {
			filtered = append(filtered, s)
		}
	}
	return filtered
}

// FindSubnetByID finds IpamSubnetType from IpamSubnets with specified id.
func (m *VirtualNetworkNetworkIpamRef) FindSubnetByID(id string) *IpamSubnetType {
	return (&(IpamSubnets{
		Subnets: m.Attr.IpamSubnets,
	})).Find(func(s *IpamSubnetType) bool {
		return s.GetSubnetUUID() == id
	})
}
