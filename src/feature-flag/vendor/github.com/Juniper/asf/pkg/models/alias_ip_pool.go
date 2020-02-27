package models

// GetIPsInSubnets returns list of alias ips which belong to provided ipam subnets
func (m *AliasIPPool) GetIPsInSubnets(ipamSubnets *IpamSubnets) ([]string, error) {
	var ipsInSubnets []string

	for _, floatingIP := range m.GetAliasIPs() {
		contains, err := ipamSubnets.Contains(floatingIP.GetAliasIPAddress())
		if err != nil {
			return nil, err
		}
		if contains {
			ipsInSubnets = append(ipsInSubnets, floatingIP.GetAliasIPAddress())
		}
	}

	return ipsInSubnets, nil
}
