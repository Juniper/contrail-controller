package models

import (
	"fmt"
	"reflect"
	"strconv"

	"github.com/pkg/errors"

	"github.com/Juniper/asf/pkg/errutil"
	"github.com/Juniper/asf/pkg/format"
)

const (
	// SRCToDSTDirection is the standard direction in PolicyRuleType.
	SRCToDSTDirection = ">"

	// TODO: Generate these from the enum in the schema.

	// IPv6Ethertype is the Ethertype for IPv6.
	IPv6Ethertype = "IPv6"
	// IPv4Ethertype is the Ethertype for IPv4.
	IPv4Ethertype = "IPv4"

	// AnyProtocol matches any protocol in PolicyRuleType.
	AnyProtocol = "any"
	// ICMPProtocol matches ICMP protocol in PolicyRuleType.
	ICMPProtocol = "icmp"
	// TCPProtocol matches TCP protocol in PolicyRuleType.
	TCPProtocol = "tcp"
	// UDPProtocol matches UDP protocol in PolicyRuleType.
	UDPProtocol = "udp"
	// ICMP6Protocol matches ICMPv6 protocol in PolicyRuleType.
	ICMP6Protocol = "icmp6"
)

// EqualRule checks if rule contains same data as other rule.
func (m PolicyRuleType) EqualRule(other PolicyRuleType) bool {
	m.RuleUUID = ""
	m.LastModified = ""
	m.Created = ""

	other.RuleUUID = ""
	other.LastModified = ""
	other.Created = ""

	return reflect.DeepEqual(m, other)
}

var avaiableProtocols = []string{AnyProtocol, ICMPProtocol, TCPProtocol, UDPProtocol, ICMP6Protocol}

var isAvailableProtocol = format.BoolMap(avaiableProtocols)

// ValidateProtocol checks if protocol is valid rule protocol.
func (m *PolicyRuleType) ValidateProtocol() error {
	proto := m.GetProtocol()

	if isAvailableProtocol[proto] {
		return nil
	}

	number, err := strconv.Atoi(proto)
	if err != nil {
		return errutil.ErrorBadRequestf("Rule with invalid protocol: %v.", proto)
	}

	if number < 0 || number > 255 {
		return errutil.ErrorBadRequestf("Rule with invalid protocol: %v.", number)
	}

	return nil
}

// PolicyAddressPair is a single combination of source and destination specifications from a PolicyRuleType.
type PolicyAddressPair struct {
	PolicyRule                        *PolicyRuleType
	SourceAddress, DestinationAddress *AddressType
	SourcePort, DestinationPort       *PortType
}

// IsIngress checks whether the pair is ingress (remote to local addresses) or egress (local to remote addresses).
func (pair *PolicyAddressPair) IsIngress() (bool, error) {
	switch {
	case pair.DestinationAddress.IsSecurityGroupLocal():
		return true, nil
	case pair.SourceAddress.IsSecurityGroupLocal():
		return false, nil
	default:
		return false, neitherAddressIsLocal{
			sourceAddress:      pair.SourceAddress,
			destinationAddress: pair.DestinationAddress,
		}
	}
}

type neitherAddressIsLocal struct {
	sourceAddress, destinationAddress *AddressType
}

func (err neitherAddressIsLocal) Error() string {
	return fmt.Sprintf("neither source nor destination address is local. Source address: %v. Destination address: %v",
		err.sourceAddress, err.destinationAddress)
}

func (m *PolicyRuleType) allAddressCombinations() (pairs []PolicyAddressPair) {
	for _, sourceAddress := range m.SRCAddresses {
		for _, sourcePort := range m.SRCPorts {
			for _, destinationAddress := range m.DSTAddresses {
				for _, destinationPort := range m.DSTPorts {
					pairs = append(pairs, PolicyAddressPair{
						PolicyRule: m,

						SourceAddress:      sourceAddress,
						SourcePort:         sourcePort,
						DestinationAddress: destinationAddress,
						DestinationPort:    destinationPort,
					})
				}
			}
		}
	}
	return pairs
}

var ipV6ProtocolStringToNumber = map[string]string{
	ICMPProtocol:  "58",
	ICMP6Protocol: "58",
	TCPProtocol:   "6",
	UDPProtocol:   "17",
}

var ipV4ProtocolStringToNumber = map[string]string{
	ICMPProtocol:  "1",
	ICMP6Protocol: "58",
	TCPProtocol:   "6",
	UDPProtocol:   "17",
}

// ValidateSubnetsWithEthertype validates if every subnet
// within source and destination addresses matches rule ethertype.
func (m *PolicyRuleType) ValidateSubnetsWithEthertype() error {
	if m.GetEthertype() != "" {
		for _, addr := range m.GetSRCAddresses() {
			if err := addr.Subnet.ValidateWithEthertype(m.GetEthertype()); err != nil {
				return err
			}
		}
		for _, addr := range m.GetDSTAddresses() {
			if err := addr.Subnet.ValidateWithEthertype(m.GetEthertype()); err != nil {
				return err
			}
		}
	}
	return nil
}

// HasSecurityGroup returns true if any of addresses points at Security Group.
func (m *PolicyRuleType) HasSecurityGroup() bool {
	for _, addr := range m.GetSRCAddresses() {
		if addr.GetSecurityGroup() != "" {
			return true
		}
	}
	for _, addr := range m.GetDSTAddresses() {
		if addr.GetSecurityGroup() != "" {
			return true
		}
	}
	return false
}

// IsAnySecurityGroupAddrLocal returns true if at least one of addresses contains
// 'local' Security Group.
func (m *PolicyRuleType) IsAnySecurityGroupAddrLocal() bool {
	for _, addr := range m.GetSRCAddresses() {
		if addr.IsSecurityGroupLocal() {
			return true
		}
	}
	for _, addr := range m.GetDSTAddresses() {
		if addr.IsSecurityGroupLocal() {
			return true
		}
	}
	return false
}

// ACLProtocol returns the protocol in a format suitable for an AclRuleType.
func (m *PolicyRuleType) ACLProtocol() (string, error) {
	protocol := m.GetProtocol()
	ethertype := m.GetEthertype()

	if protocol == "" || protocol == AnyProtocol || isNumeric(protocol) {
		return protocol, nil
	}

	protocol, err := numericProtocolForEthertype(protocol, ethertype)
	if err != nil {
		return "", errors.Wrap(err, "failed to convert protocol for an ACL")
	}
	return protocol, nil
}

func isNumeric(s string) bool {
	_, err := strconv.Atoi(s)
	return err == nil
}

func numericProtocolForEthertype(protocol, ethertype string) (numericProtocol string, err error) {
	var ok bool
	if ethertype == IPv6Ethertype {
		numericProtocol, ok = ipV6ProtocolStringToNumber[protocol]
	} else {
		numericProtocol, ok = ipV4ProtocolStringToNumber[protocol]
	}

	if !ok {
		return "", errors.Errorf("unknown protocol %q for ethertype %q", protocol, ethertype)
	}
	return numericProtocol, nil
}
