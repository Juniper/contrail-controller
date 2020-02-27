package models

import (
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestIsIngress(t *testing.T) {
	testCases := []struct {
		name string
		PolicyAddressPair
		isIngress bool
		err       error
	}{
		{
			name: "specified security group to local security group",
			PolicyAddressPair: PolicyAddressPair{
				SourceAddress: &AddressType{
					SecurityGroup: "default-domain:project-blue:default",
				},
				DestinationAddress: &AddressType{
					SecurityGroup: "local",
				},
			},
			isIngress: true,
		},
		{
			name: "local security group to all IPv4 addresses",
			PolicyAddressPair: PolicyAddressPair{
				SourceAddress: &AddressType{
					SecurityGroup: "local",
				},
				DestinationAddress: AllIPv4Addresses(),
			},
			isIngress: false,
		},
		{
			name: "local security group to all IPv6 addresses",
			PolicyAddressPair: PolicyAddressPair{
				SourceAddress: &AddressType{
					SecurityGroup: "local",
				},
				DestinationAddress: AllIPv6Addresses(),
			},
			isIngress: false,
		},
		{
			name: "both with local security group",
			PolicyAddressPair: PolicyAddressPair{
				SourceAddress: &AddressType{
					SecurityGroup: "local",
				},
				DestinationAddress: &AddressType{
					SecurityGroup: "local",
				},
			},
			// https://github.com/Juniper/asf-controller/blob/08f2b11d3/src/config/schema-transformer/config_db.py#L2030
			isIngress: true,
		},
		{
			name: "neither with local security group",
			PolicyAddressPair: PolicyAddressPair{
				SourceAddress:      &AddressType{},
				DestinationAddress: &AddressType{},
			},
			err: neitherAddressIsLocal{
				sourceAddress:      &AddressType{},
				destinationAddress: &AddressType{},
			},
		},
	}

	for _, tt := range testCases {
		t.Run(tt.name, func(t *testing.T) {
			isIngress, err := tt.PolicyAddressPair.IsIngress()
			if tt.err != nil {
				assert.Equal(t, tt.err, err)
			} else {
				assert.NoError(t, err)
				assert.Equal(t, tt.isIngress, isIngress)
			}
		})
	}
}

func TestIsLocal(t *testing.T) {
	testCases := []struct {
		name    string
		address *AddressType
		is      bool
	}{
		{
			name: "local security group",
			address: &AddressType{
				SecurityGroup: "local",
			},
			is: true,
		},
		{
			name: "specified security group",
			address: &AddressType{
				SecurityGroup: "default-domain:project-blue:default",
			},
			is: false,
		},
		{
			name:    "all IPv4 addresses",
			address: AllIPv4Addresses(),
			is:      false,
		},
		{
			name:    "all IPv6 addresses",
			address: AllIPv6Addresses(),
			is:      false,
		},
	}

	for _, tt := range testCases {
		t.Run(tt.name, func(t *testing.T) {
			assert.Equal(t, tt.is, tt.address.IsSecurityGroupLocal())
		})
	}
}

func TestACLProtocol(t *testing.T) {
	testCases := []struct {
		name                string
		policyRule          *PolicyRuleType
		expectedACLProtocol string
		fails               bool
	}{
		{
			name: "any",
			policyRule: &PolicyRuleType{
				Protocol: AnyProtocol,
			},
			expectedACLProtocol: AnyProtocol,
		},

		{
			name: "not specified",
			policyRule: &PolicyRuleType{
				Protocol: "",
			},
			expectedACLProtocol: "",
		},

		{
			name: "already a number",
			policyRule: &PolicyRuleType{
				Protocol: "58",
			},
			expectedACLProtocol: "58",
		},

		{
			name: "unknown IPv6 protocol",
			policyRule: &PolicyRuleType{
				Protocol:  "some unknown protocol",
				Ethertype: IPv6Ethertype,
			},
			fails: true,
		},

		{
			name: "unknown IPv4 protocol",
			policyRule: &PolicyRuleType{
				Protocol:  "some unknown protocol",
				Ethertype: IPv4Ethertype,
			},
			fails: true,
		},

		{
			name: "unknown ethertype and protocol",
			policyRule: &PolicyRuleType{
				Protocol:  "some unknown protocol",
				Ethertype: "some unknown ethertype",
			},
			fails: true,
		},

		{
			name: "icmp ipv6",
			policyRule: &PolicyRuleType{
				Protocol:  ICMPProtocol,
				Ethertype: IPv6Ethertype,
			},
			expectedACLProtocol: "58",
		},

		{
			name: "icmp ipv4",
			policyRule: &PolicyRuleType{
				Protocol:  ICMPProtocol,
				Ethertype: IPv4Ethertype,
			},
			expectedACLProtocol: "1",
		},

		// The rest of the tests are the same for IPv6 and IPv4
		{
			name: "icmp6 ipv6",
			policyRule: &PolicyRuleType{
				Protocol:  ICMP6Protocol,
				Ethertype: IPv6Ethertype,
			},
			expectedACLProtocol: "58",
		},

		{
			name: "icmp6 ipv4",
			policyRule: &PolicyRuleType{
				Protocol:  ICMP6Protocol,
				Ethertype: IPv4Ethertype,
			},
			expectedACLProtocol: "58",
		},

		{
			name: "tcp ipv6",
			policyRule: &PolicyRuleType{
				Protocol:  TCPProtocol,
				Ethertype: IPv6Ethertype,
			},
			expectedACLProtocol: "6",
		},

		{
			name: "tcp ipv4",
			policyRule: &PolicyRuleType{
				Protocol:  TCPProtocol,
				Ethertype: IPv4Ethertype,
			},
			expectedACLProtocol: "6",
		},

		{
			name: "udp ipv6",
			policyRule: &PolicyRuleType{
				Protocol:  UDPProtocol,
				Ethertype: IPv6Ethertype,
			},
			expectedACLProtocol: "17",
		},

		{
			name: "udp ipv4",
			policyRule: &PolicyRuleType{
				Protocol:  UDPProtocol,
				Ethertype: IPv4Ethertype,
			},
			expectedACLProtocol: "17",
		},
	}

	for _, tt := range testCases {
		t.Run(tt.name, func(t *testing.T) {
			aclProtocol, err := tt.policyRule.ACLProtocol()
			if tt.fails {
				assert.Error(t, err)
			} else {
				assert.NoError(t, err)
			}
			assert.Equal(t, tt.expectedACLProtocol, aclProtocol)
		})
	}
}
