package models

const (
	// AnySecurityGroup matches any security group in AddressType.
	AnySecurityGroup = "any"
	// LocalSecurityGroup matches the local security group in AddressType.
	LocalSecurityGroup = "local"
	// UnspecifiedSecurityGroup doesn't match against the security group in AddressType.
	UnspecifiedSecurityGroup = ""

	// IPv4ZeroValue is the zero IPv4 address.
	IPv4ZeroValue = "0.0.0.0"
	// IPv6ZeroValue is the zero IPv6 address.
	IPv6ZeroValue = "::"
)

// AllIPv4Addresses returns an AddressType with a subnet of all possible IPv4 addresses.
func AllIPv4Addresses() *AddressType {
	return &AddressType{
		Subnet: &SubnetType{
			IPPrefix:    IPv4ZeroValue,
			IPPrefixLen: 0,
		},
	}
}

// AllIPv6Addresses returns an AddressType with a subnet of all possible IPv6 addresses.
func AllIPv6Addresses() *AddressType {
	return &AddressType{
		Subnet: &SubnetType{
			IPPrefix:    IPv6ZeroValue,
			IPPrefixLen: 0,
		},
	}
}

// IsSecurityGroupNameAReference checks if the Security Group name in an address
// is a reference to other security group.
func (m *AddressType) IsSecurityGroupNameAReference() bool {
	return m.SecurityGroup != AnySecurityGroup &&
		m.SecurityGroup != LocalSecurityGroup && m.SecurityGroup != UnspecifiedSecurityGroup
}

// IsSecurityGroupLocal checks if the address specifies the local Security Group.
func (m *AddressType) IsSecurityGroupLocal() bool {
	return m.SecurityGroup == LocalSecurityGroup
}
