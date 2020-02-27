package models

// UUIDs returns subnets' UUIDs.
func (m *IpamSubnets) UUIDs() (uuids []string) {
	for _, s := range m.GetSubnets() {
		uuids = append(uuids, s.SubnetUUID)
	}

	return uuids
}

// Find returns first subnet in IpamSubnets that matches given predicate.
// If no subnet is found, nil is returned.
func (m *IpamSubnets) Find(pred func(*IpamSubnetType) bool) *IpamSubnetType {
	for _, s := range m.GetSubnets() {
		if pred(s) {
			return s
		}
	}
	return nil
}

// Contains checks if IpamSubnets contain provided IP address.
func (m *IpamSubnets) Contains(ipString string) (ok bool, err error) {
	ip, err := parseIP(ipString)
	if err != nil {
		return false, err
	}

	s := m.Find(func(sub *IpamSubnetType) bool {
		var contains bool
		contains, err = sub.Contains(ip)
		return err == nil && contains
	})

	return s != nil, err
}

// Subtract subtracts right set from IpamSubnets set and returns the result.
func (m *IpamSubnets) Subtract(rightSet *IpamSubnets) *IpamSubnets {
	if m == nil {
		return nil
	}

	var subnets []*IpamSubnetType
	rightMap := make(map[string]bool)
	for _, r := range rightSet.GetSubnets() {
		rightMap[r.SubnetUUID] = true
	}

	for _, l := range m.GetSubnets() {
		if !rightMap[l.SubnetUUID] {
			subnets = append(subnets, l)
		}
	}

	return &IpamSubnets{
		Subnets: subnets,
	}
}
