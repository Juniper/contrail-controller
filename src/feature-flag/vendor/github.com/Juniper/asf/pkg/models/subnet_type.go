package models

import (
	"fmt"
	"net"

	"github.com/Juniper/asf/pkg/errutil"
)

// CIDR returns classless inter-domain routing of a subnet.
func (s *SubnetType) CIDR() string {
	return fmt.Sprintf("%s/%d", s.GetIPPrefix(), s.GetIPPrefixLen())
}

// ValidateWithEthertype checks if IP version from CIDR matches ethertype
// and throws an error if it doesn't.
func (s *SubnetType) ValidateWithEthertype(ethertype string) error {
	if s == nil {
		return nil
	}
	cidr := s.CIDR()
	version, err := resolveIPVersionFromCIDR(cidr)
	if err != nil {
		return err
	}
	if ethertype != version {
		return errutil.ErrorBadRequestf(
			"Rule subnet %v doesn't match ethertype %v", cidr, ethertype,
		)
	}
	return nil
}

func resolveIPVersionFromCIDR(cidr string) (string, error) {
	network, _, err := net.ParseCIDR(cidr)
	if err != nil {
		return "", errutil.ErrorBadRequestf("Cannot parse address %v. %v.", cidr, err)
	}
	switch {
	case network.To4() != nil:
		return "IPv4", nil
	case network.To16() != nil:
		return "IPv6", nil
	default:
		return "", errutil.ErrorBadRequestf("Cannot resolve ip version %v.", cidr)
	}
}
