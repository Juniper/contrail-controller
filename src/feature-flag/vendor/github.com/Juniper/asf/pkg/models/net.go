package models

import (
	"net"

	"github.com/pkg/errors"
)

func parseIP(ipString string) (net.IP, error) {
	ip := net.ParseIP(ipString)
	if ip == nil {
		return nil, errors.Errorf("couldn't parse IP address: " + ipString)
	}
	return ip, nil
}

func isIPInSubnet(subnet *net.IPNet, ipString string) error {
	ip, err := parseIP(ipString)
	if err != nil {
		return err
	}
	if !subnet.Contains(ip) {
		return errors.Errorf("address is out of CIDR: " + subnet.String())
	}
	return nil
}

// CheckSubnetsOverlap checks if subnets overlaps.
func CheckSubnetsOverlap(cidr1, cidr2 string) (bool, error) {
	ip1, sub1, err := net.ParseCIDR(cidr1)
	if err != nil {
		return false, err
	}

	ip2, sub2, err := net.ParseCIDR(cidr2)
	if err != nil {
		return false, err
	}

	if sub1.Contains(ip2) || sub2.Contains(ip1) {
		return true, nil
	}
	return false, nil
}
