package models

import (
	"github.com/pkg/errors"
)

// ValidateFlatSubnet validates flat subnet.
func (m *VnSubnetsType) ValidateFlatSubnet() error {
	for _, ipamSubnet := range m.GetIpamSubnets() {
		if ipamSubnet.Subnet.IPPrefix != "" {
			return errors.New("with flat-subnet, network can not have user-defined subnet")
		}
	}
	return nil
}

// ValidateUserDefined validates user defined subnet.
func (m *VnSubnetsType) ValidateUserDefined() error {
	for _, ipamSubnet := range m.GetIpamSubnets() {
		// check network subnet
		err := ipamSubnet.Validate()
		if err != nil {
			return err
		}
	}
	return nil
}

// IsFlatSubnet checks if linked ipam subnet is flat
func (m *VnSubnetsType) IsFlatSubnet() bool {
	vnIpamSubnets := m.GetIpamSubnets()
	if len(vnIpamSubnets) != 1 || vnIpamSubnets[0].GetSubnet().GetIPPrefix() != "" {
		return false
	}
	return true
}
