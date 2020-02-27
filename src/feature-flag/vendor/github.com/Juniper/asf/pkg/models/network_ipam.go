package models

import (
	"bytes"
	"net"
	"strconv"

	"github.com/Juniper/asf/pkg/errutil"
	"github.com/pkg/errors"

	uuid "github.com/satori/go.uuid"
)

// IPAM subnet methods.
const (
	UserDefinedSubnet = "user-defined-subnet"
	AutoSubnet        = "auto-subnet"
	FlatSubnet        = "flat-subnet"
)

// IsFlatSubnet checks if this is flat subnet.
func (m *NetworkIpam) IsFlatSubnet() bool {
	return m.IpamSubnetMethod == FlatSubnet
}

// ContainsAllocationPool checks if ipam contains provided allocation pool.
func (m *NetworkIpam) ContainsAllocationPool(allocPool *AllocationPoolType) bool {
	for _, subnet := range m.GetIpamSubnets().GetSubnets() {
		for _, ap := range subnet.GetAllocationPools() {
			if *allocPool == *ap {
				return true
			}
		}
	}

	return false
}

// Net returns IPNet object for this subnet.
func (m *SubnetType) Net() (*net.IPNet, error) {
	cidr := m.IPPrefix + "/" + strconv.Itoa(int(m.IPPrefixLen))
	_, n, err := net.ParseCIDR(cidr)
	return n, errors.Wrap(err, "couldn't parse cidr")
}

// Validate subnet type.
func (m *SubnetType) Validate() error {
	if m.IPPrefix == "" {
		return errors.New("IP prefix is empty")
	}
	_, err := m.Net()
	return err
}

// IsInSubnet validates allocation pool is in specific subnet.
func (m *AllocationPoolType) IsInSubnet(subnet *net.IPNet) error {
	err := isIPInSubnet(subnet, m.Start)
	if err != nil {
		return errutil.ErrorBadRequest("allocation pool start " + err.Error())
	}
	err = isIPInSubnet(subnet, m.End)
	if err != nil {
		return errutil.ErrorBadRequest("allocation pool end " + err.Error())
	}
	return nil
}

// AllocationPoolsSubtract subtract allocation pools.
func AllocationPoolsSubtract(
	left []*AllocationPoolType, right []*AllocationPoolType,
) (res []*AllocationPoolType) {

	isPresentInRight := make(map[string]bool)
	hashFunc := func(ap *AllocationPoolType) string {
		return ap.Start + ap.End
	}

	for _, r := range right {
		isPresentInRight[hashFunc(r)] = true
	}

	for _, l := range left {
		if !isPresentInRight[hashFunc(l)] {
			res = append(res, l)
		}
	}

	return res
}

// ContainsIPAddress if ip address string belongs to allocation pool.
func (m *AllocationPoolType) ContainsIPAddress(ipAddress string) (bool, error) {
	ip, err := parseIP(ipAddress)
	if err != nil {
		return false, err
	}

	return m.Contains(ip)
}

// Contains checks if ip address belongs to allocation pool.
func (m *AllocationPoolType) Contains(ip net.IP) (bool, error) {
	startIP := net.ParseIP(m.Start)
	if startIP == nil {
		return false, errors.Errorf("couldn't parse start ip address: %v", m.Start)
	}

	endIP := net.ParseIP(m.End)
	if endIP == nil {
		return false, errors.Errorf("couldn't parse end ip address: %v", m.End)
	}

	if bytes.Compare(startIP.To16(), ip.To16()) <= 0 && bytes.Compare(ip.To16(), endIP.To16()) <= 0 {
		return true, nil
	}
	return false, nil
}

// Validate validates ipam subnet configuration.
func (m *IpamSubnetType) Validate() error {
	if m.SubnetUUID != "" {
		if _, err := uuid.FromString(m.SubnetUUID); err != nil {
			return errutil.ErrorBadRequestf("invalid subnet uuid: %v", err)
		}
	}

	return m.ValidateSubnetParams()
}

// ValidateSubnetParams validates ipam subnet params.
func (m *IpamSubnetType) ValidateSubnetParams() error {
	subnet, err := m.Subnet.Net()
	if err != nil {
		return errutil.ErrorBadRequestf("invalid subnet: %v", err)
	}

	for _, allocationPool := range m.AllocationPools {
		err = allocationPool.IsInSubnet(subnet)
		if err != nil {
			return err
		}
	}
	if m.DefaultGateway != "" {
		err = isIPInSubnet(subnet, m.DefaultGateway)
		if err != nil {
			return errutil.ErrorBadRequest("default gateway " + err.Error())
		}
	}
	if m.DNSServerAddress != "" {
		_, err = parseIP(m.DNSServerAddress)
		if err != nil {
			return errutil.ErrorBadRequest("DNS server " + err.Error())
		}
	}
	return nil
}

// Contains checks if IpamSubnet contains provided ip
func (m *IpamSubnetType) Contains(ip net.IP) (bool, error) {
	if contains, err := m.ContainsWithinSubnetCIDR(ip); !contains || err != nil {
		return false, err
	}

	if len(m.GetAllocationPools()) == 0 {
		return true, nil
	}
	return m.ContainsWithinAllocationPools(ip)
}

// ContainsWithinSubnetCIDR checks if given IP exists within SubnetCIDR
func (m *IpamSubnetType) ContainsWithinSubnetCIDR(ip net.IP) (bool, error) {
	if m.Subnet == nil {
		return false, nil
	}

	subnet, err := m.Subnet.Net()
	if err != nil {
		return false, errors.Errorf("invalid subnet: %v", err)
	}

	return subnet.Contains(ip), nil
}

// ContainsWithinAllocationPools checks if given IP exists within Allocation Pools
func (m *IpamSubnetType) ContainsWithinAllocationPools(ip net.IP) (bool, error) {
	for _, pool := range m.GetAllocationPools() {
		contains, err := pool.Contains(ip)
		if err != nil {
			return false, err
		}
		if contains {
			return true, nil
		}
	}

	return false, nil
}
