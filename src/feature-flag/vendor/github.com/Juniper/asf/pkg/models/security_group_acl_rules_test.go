package models

import (
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestToACLRules(t *testing.T) {
	testCases := []struct {
		name        string
		policyRules *PolicyRulesWithRefs

		expectedIngressACLRules []*AclRuleType
		expectedEgressACLRules  []*AclRuleType
	}{
		{
			// Behave properly, unlike
			// https://github.com/Juniper/asf-controller/blob/be4053c84/src/config/schema-transformer/config_db.py#L2030
			name: "Non-local destination address after a local destination address",
			policyRules: &PolicyRulesWithRefs{
				Rules: []*PolicyRuleType{
					{
						Direction: SRCToDSTDirection,
						Protocol:  AnyProtocol,
						RuleUUID:  "rule1",
						Ethertype: IPv4Ethertype,
						SRCAddresses: []*AddressType{
							{
								SecurityGroup: LocalSecurityGroup,
							},
						},
						DSTAddresses: []*AddressType{
							AllIPv4Addresses(),
							{
								SecurityGroup: LocalSecurityGroup,
							},
							AllIPv4Addresses(),
						},
						SRCPorts: []*PortType{AllPorts()},
						DSTPorts: []*PortType{AllPorts()},
					},
				},
			},

			expectedIngressACLRules: []*AclRuleType{
				{
					RuleUUID: "rule1",
					MatchCondition: &MatchConditionType{
						SRCPort:    AllPorts(),
						DSTPort:    AllPorts(),
						Protocol:   AnyProtocol,
						Ethertype:  IPv4Ethertype,
						SRCAddress: &AddressType{},
						DSTAddress: &AddressType{},
					},
					ActionList: &ActionListType{
						SimpleAction: "pass",
					},
				},
			},

			expectedEgressACLRules: []*AclRuleType{
				{
					RuleUUID: "rule1",
					MatchCondition: &MatchConditionType{
						SRCPort:    AllPorts(),
						DSTPort:    AllPorts(),
						Protocol:   AnyProtocol,
						Ethertype:  IPv4Ethertype,
						SRCAddress: &AddressType{},
						DSTAddress: AllIPv4Addresses(),
					},
					ActionList: &ActionListType{
						SimpleAction: "pass",
					},
				},
				{
					RuleUUID: "rule1",
					MatchCondition: &MatchConditionType{
						SRCPort:    AllPorts(),
						DSTPort:    AllPorts(),
						Protocol:   AnyProtocol,
						Ethertype:  IPv4Ethertype,
						SRCAddress: &AddressType{},
						DSTAddress: AllIPv4Addresses(),
					},
					ActionList: &ActionListType{
						SimpleAction: "pass",
					},
				},
			},
		},

		{
			// Behave properly, unlike
			// https://github.com/Juniper/asf-controller/blob/be4053c84/src/config/schema-transformer/config_db.py#L2030
			name: "Non-local source & destination addresses after a local source address",
			policyRules: &PolicyRulesWithRefs{
				Rules: []*PolicyRuleType{
					{
						Direction: SRCToDSTDirection,
						Protocol:  AnyProtocol,
						RuleUUID:  "rule1",
						Ethertype: IPv4Ethertype,
						SRCAddresses: []*AddressType{
							{
								SecurityGroup: LocalSecurityGroup,
							},
							AllIPv4Addresses(),
						},
						DSTAddresses: []*AddressType{
							AllIPv4Addresses(),
						},
						SRCPorts: []*PortType{AllPorts()},
						DSTPorts: []*PortType{AllPorts()},
					},
				},
			},

			expectedIngressACLRules: nil,

			expectedEgressACLRules: []*AclRuleType{
				{
					RuleUUID: "rule1",
					MatchCondition: &MatchConditionType{
						SRCPort:    AllPorts(),
						DSTPort:    AllPorts(),
						Protocol:   AnyProtocol,
						Ethertype:  IPv4Ethertype,
						SRCAddress: &AddressType{},
						DSTAddress: AllIPv4Addresses(),
					},
					ActionList: &ActionListType{
						SimpleAction: "pass",
					},
				},
			},
		},

		{
			// Behave properly, unlike
			// https://github.com/Juniper/asf-controller/blob/be4053c84/src/config/schema-transformer/config_db.py#L2030
			name: "Non-local source & destination addresses after a local destination address",
			policyRules: &PolicyRulesWithRefs{
				Rules: []*PolicyRuleType{
					{
						Direction: SRCToDSTDirection,
						Protocol:  AnyProtocol,
						RuleUUID:  "rule1",
						Ethertype: IPv4Ethertype,
						SRCAddresses: []*AddressType{
							{
								SecurityGroup: LocalSecurityGroup,
							},
							AllIPv4Addresses(),
						},
						DSTAddresses: []*AddressType{
							AllIPv4Addresses(),
						},
						SRCPorts: []*PortType{AllPorts()},
						DSTPorts: []*PortType{AllPorts()},
					},
				},
			},

			expectedIngressACLRules: nil,

			expectedEgressACLRules: []*AclRuleType{
				{
					RuleUUID: "rule1",
					MatchCondition: &MatchConditionType{
						SRCPort:    AllPorts(),
						DSTPort:    AllPorts(),
						Protocol:   AnyProtocol,
						Ethertype:  IPv4Ethertype,
						SRCAddress: &AddressType{},
						DSTAddress: AllIPv4Addresses(),
					},
					ActionList: &ActionListType{
						SimpleAction: "pass",
					},
				},
			},
		},

		{
			name: "Unknown IPv4 protocol in the only rule",
			policyRules: &PolicyRulesWithRefs{
				Rules: []*PolicyRuleType{
					{
						Direction: SRCToDSTDirection,
						Protocol:  "some unknown protocol",
						RuleUUID:  "rule1",
						Ethertype: IPv4Ethertype,
						SRCAddresses: []*AddressType{
							{
								SecurityGroup: LocalSecurityGroup,
							},
						},
						DSTAddresses: []*AddressType{
							AllIPv4Addresses(),
							{
								SecurityGroup: LocalSecurityGroup,
							},
						},
						SRCPorts: []*PortType{AllPorts()},
						DSTPorts: []*PortType{AllPorts()},
					},
				},
			},

			expectedIngressACLRules: nil,
			expectedEgressACLRules:  nil,
		},

		{
			name: "Unknown IPv4 protocol in one of the rules",
			policyRules: &PolicyRulesWithRefs{
				Rules: []*PolicyRuleType{
					{
						Protocol:  "unknown protocol 1",
						RuleUUID:  "rule1",
						Ethertype: IPv4Ethertype,
						SRCAddresses: []*AddressType{
							{
								SecurityGroup: LocalSecurityGroup,
							},
						},
						DSTAddresses: []*AddressType{
							AllIPv4Addresses(),
							{
								SecurityGroup: LocalSecurityGroup,
							},
						},
						SRCPorts: []*PortType{AllPorts()},
						DSTPorts: []*PortType{AllPorts()},
					},
					{
						Direction: SRCToDSTDirection,
						Protocol:  AnyProtocol,
						RuleUUID:  "rule2",
						Ethertype: IPv6Ethertype,
						SRCAddresses: []*AddressType{
							{
								SecurityGroup: LocalSecurityGroup,
							},
						},
						DSTAddresses: []*AddressType{
							AllIPv6Addresses(),
							{
								SecurityGroup: LocalSecurityGroup,
							},
						},
						SRCPorts: []*PortType{AllPorts()},
						DSTPorts: []*PortType{AllPorts()},
					},
				},
			},

			expectedIngressACLRules: []*AclRuleType{
				{
					RuleUUID: "rule2",
					MatchCondition: &MatchConditionType{
						SRCPort:    AllPorts(),
						DSTPort:    AllPorts(),
						Protocol:   AnyProtocol,
						Ethertype:  IPv6Ethertype,
						SRCAddress: &AddressType{},
						DSTAddress: &AddressType{},
					},
					ActionList: &ActionListType{
						SimpleAction: "pass",
					},
				},
			},
			expectedEgressACLRules: []*AclRuleType{
				{
					RuleUUID: "rule2",
					MatchCondition: &MatchConditionType{
						SRCPort:    AllPorts(),
						DSTPort:    AllPorts(),
						Protocol:   AnyProtocol,
						Ethertype:  IPv6Ethertype,
						SRCAddress: &AddressType{},
						DSTAddress: AllIPv6Addresses(),
					},
					ActionList: &ActionListType{
						SimpleAction: "pass",
					},
				},
			},
		},

		{
			name: "unknown security group name in one of the addresses",
			policyRules: &PolicyRulesWithRefs{
				Rules: []*PolicyRuleType{
					{
						Direction: SRCToDSTDirection,
						Protocol:  AnyProtocol,
						RuleUUID:  "rule1",
						Ethertype: IPv6Ethertype,
						SRCAddresses: []*AddressType{
							{
								SecurityGroup: LocalSecurityGroup,
							},
						},
						DSTAddresses: []*AddressType{
							AllIPv6Addresses(),
							{
								SecurityGroup: "some:unknown:security-group",
							},
						},
						SRCPorts: []*PortType{AllPorts()},
						DSTPorts: []*PortType{AllPorts()},
					},
				},
			},

			expectedIngressACLRules: nil,

			expectedEgressACLRules: []*AclRuleType{
				{
					RuleUUID: "rule1",
					MatchCondition: &MatchConditionType{
						SRCPort:    AllPorts(),
						DSTPort:    AllPorts(),
						Protocol:   AnyProtocol,
						Ethertype:  IPv6Ethertype,
						SRCAddress: &AddressType{},
						DSTAddress: AllIPv6Addresses(),
					},
					ActionList: &ActionListType{
						SimpleAction: "pass",
					},
				},
			},
		},

		{
			name: "reference to existing security group",
			policyRules: &PolicyRulesWithRefs{
				Rules: []*PolicyRuleType{
					{
						Direction: SRCToDSTDirection,
						Protocol:  AnyProtocol,
						RuleUUID:  "rule1",
						Ethertype: IPv6Ethertype,
						SRCAddresses: []*AddressType{
							{
								SecurityGroup: LocalSecurityGroup,
							},
						},
						DSTAddresses: []*AddressType{
							{
								SecurityGroup: "some:known:security-group",
							},
						},
						SRCPorts: []*PortType{AllPorts()},
						DSTPorts: []*PortType{AllPorts()},
					},
				},
				FQNameToSG: map[string]*SecurityGroup{
					"some:known:security-group": {
						SecurityGroupID: 8000002,
					},
				},
			},

			expectedIngressACLRules: nil,

			expectedEgressACLRules: []*AclRuleType{
				{
					RuleUUID: "rule1",
					MatchCondition: &MatchConditionType{
						SRCPort:    AllPorts(),
						DSTPort:    AllPorts(),
						Protocol:   AnyProtocol,
						Ethertype:  IPv6Ethertype,
						SRCAddress: &AddressType{},
						DSTAddress: &AddressType{SecurityGroup: "8000002"},
					},
					ActionList: &ActionListType{
						SimpleAction: "pass",
					},
				},
			},
		},
	}

	for _, tt := range testCases {
		t.Run(tt.name, func(t *testing.T) {
			ingressACLRules, egressACLRules := tt.policyRules.ToACLRules()
			assert.Equal(t, tt.expectedIngressACLRules, ingressACLRules,
				"ingress ACL rules don't match the expected ones")
			assert.Equal(t, tt.expectedEgressACLRules, egressACLRules,
				"egress ACL rules don't match the expected ones")
		})
	}
}

func TestMakeACLRule(t *testing.T) {
	testCases := []struct {
		name       string
		fqNameToSG map[string]*SecurityGroup
		PolicyAddressPair

		expectedACLRule *AclRuleType
		fails           bool
	}{
		{
			name: "IPv4, specified security group to local security group",
			fqNameToSG: map[string]*SecurityGroup{
				"default-domain:project-blue:default": {
					SecurityGroupID: 8000002,
				},
			},
			PolicyAddressPair: PolicyAddressPair{
				PolicyRule: &PolicyRuleType{
					RuleUUID:  "bdf042c0-d2c2-4241-ba15-1c702c896e03",
					Direction: SRCToDSTDirection,
					Protocol:  AnyProtocol,
					Ethertype: IPv4Ethertype,
				},
				SourceAddress: &AddressType{
					SecurityGroup: "default-domain:project-blue:default",
				},
				DestinationAddress: &AddressType{
					SecurityGroup: LocalSecurityGroup,
				},
				SourcePort:      AllPorts(),
				DestinationPort: AllPorts(),
			},

			expectedACLRule: &AclRuleType{
				RuleUUID: "bdf042c0-d2c2-4241-ba15-1c702c896e03",
				MatchCondition: &MatchConditionType{
					SRCPort:   AllPorts(),
					DSTPort:   AllPorts(),
					Protocol:  AnyProtocol,
					Ethertype: IPv4Ethertype,
					SRCAddress: &AddressType{
						SecurityGroup: "8000002",
					},
					DSTAddress: &AddressType{},
				},
				ActionList: &ActionListType{
					SimpleAction: "pass",
				},
			},
		},

		{
			name: "IPv6, specified security group to local security group",
			fqNameToSG: map[string]*SecurityGroup{
				"default-domain:project-blue:default": {
					SecurityGroupID: 8000002,
				},
			},
			PolicyAddressPair: PolicyAddressPair{
				PolicyRule: &PolicyRuleType{
					RuleUUID:  "1f77914a-0863-4f0d-888a-aee6a1988f6a",
					Direction: SRCToDSTDirection,
					Protocol:  AnyProtocol,
					Ethertype: IPv6Ethertype,
				},
				SourceAddress: &AddressType{
					SecurityGroup: "default-domain:project-blue:default",
				},
				DestinationAddress: &AddressType{
					SecurityGroup: LocalSecurityGroup,
				},
				SourcePort:      AllPorts(),
				DestinationPort: AllPorts(),
			},

			expectedACLRule: &AclRuleType{
				RuleUUID: "1f77914a-0863-4f0d-888a-aee6a1988f6a",
				MatchCondition: &MatchConditionType{
					SRCPort:   AllPorts(),
					DSTPort:   AllPorts(),
					Protocol:  AnyProtocol,
					Ethertype: IPv6Ethertype,
					SRCAddress: &AddressType{
						SecurityGroup: "8000002",
					},
					DSTAddress: &AddressType{},
				},
				ActionList: &ActionListType{
					SimpleAction: "pass",
				},
			},
		},

		{
			name: "IPv4, local security group to all addresses",
			PolicyAddressPair: PolicyAddressPair{
				PolicyRule: &PolicyRuleType{
					RuleUUID:  "b7c07625-e03e-43b9-a9fc-d11a6c863cc6",
					Direction: SRCToDSTDirection,
					Protocol:  AnyProtocol,
					Ethertype: IPv4Ethertype,
				},
				SourceAddress: &AddressType{
					SecurityGroup: LocalSecurityGroup,
				},
				DestinationAddress: AllIPv4Addresses(),
				SourcePort:         AllPorts(),
				DestinationPort:    AllPorts(),
			},

			expectedACLRule: &AclRuleType{
				RuleUUID: "b7c07625-e03e-43b9-a9fc-d11a6c863cc6",
				MatchCondition: &MatchConditionType{
					SRCPort:    AllPorts(),
					DSTPort:    AllPorts(),
					Protocol:   AnyProtocol,
					Ethertype:  IPv4Ethertype,
					SRCAddress: &AddressType{},
					DSTAddress: AllIPv4Addresses(),
				},
				ActionList: &ActionListType{
					SimpleAction: "pass",
				},
			},
		},

		{
			name: "IPv6, local security group to all addresses",
			PolicyAddressPair: PolicyAddressPair{
				PolicyRule: &PolicyRuleType{
					RuleUUID:  "6a5f3026-02bc-4ba1-abde-39abafd21f47",
					Direction: SRCToDSTDirection,
					Protocol:  AnyProtocol,
					Ethertype: IPv6Ethertype,
				},
				SourceAddress: &AddressType{
					SecurityGroup: LocalSecurityGroup,
				},
				DestinationAddress: AllIPv6Addresses(),
				SourcePort:         AllPorts(),
				DestinationPort:    AllPorts(),
			},

			expectedACLRule: &AclRuleType{
				RuleUUID: "6a5f3026-02bc-4ba1-abde-39abafd21f47",
				MatchCondition: &MatchConditionType{
					SRCPort:    AllPorts(),
					DSTPort:    AllPorts(),
					Protocol:   AnyProtocol,
					Ethertype:  IPv6Ethertype,
					SRCAddress: &AddressType{},
					DSTAddress: AllIPv6Addresses(),
				},
				ActionList: &ActionListType{
					SimpleAction: "pass",
				},
			},
		},

		{
			// Replicates the logic in
			// https://github.com/Juniper/asf-controller/blob/474731ce0/src/config/schema-transformer/config_db.py#L2039
			name: "ActionList with a deny action (should be ignored)",
			PolicyAddressPair: PolicyAddressPair{
				PolicyRule: &PolicyRuleType{
					RuleUUID:  "rule2",
					Direction: SRCToDSTDirection,
					Protocol:  AnyProtocol,
					Ethertype: IPv4Ethertype,
					ActionList: &ActionListType{
						SimpleAction: "deny",
					},
				},
				SourceAddress: &AddressType{
					SecurityGroup: LocalSecurityGroup,
				},
				DestinationAddress: AllIPv4Addresses(),
				SourcePort:         AllPorts(),
				DestinationPort:    AllPorts(),
			},

			expectedACLRule: &AclRuleType{
				RuleUUID: "rule2",
				MatchCondition: &MatchConditionType{
					SRCPort:    AllPorts(),
					DSTPort:    AllPorts(),
					Protocol:   AnyProtocol,
					Ethertype:  IPv4Ethertype,
					SRCAddress: &AddressType{},
					DSTAddress: AllIPv4Addresses(),
				},
				ActionList: &ActionListType{
					SimpleAction: "pass",
				},
			},
		},

		{
			name: "IPv4, unknown protocol",
			PolicyAddressPair: PolicyAddressPair{
				PolicyRule: &PolicyRuleType{
					Protocol:  "some unknown protocol",
					Ethertype: IPv4Ethertype,
				},
			},
			fails: true,
		},

		{
			name: "unknown security group name",
			PolicyAddressPair: PolicyAddressPair{
				SourceAddress: &AddressType{
					SecurityGroup: "some:unknown:security-group",
				},
			},
			fails: true,
		},
	}

	for _, tt := range testCases {
		t.Run(tt.name, func(t *testing.T) {
			aclRule, err := makeACLRule(tt.PolicyAddressPair, tt.fqNameToSG)
			if tt.fails {
				assert.Error(t, err)
			} else {
				assert.NoError(t, err)
			}
			assert.Equal(t, tt.expectedACLRule, aclRule)
		})
	}
}

func TestSecurityGroupNameToID(t *testing.T) {
	testCases := []struct {
		name              string
		securityGroupName string
		fqNameToSG        map[string]*SecurityGroup

		expectedSecurityGroupID string
		fails                   bool
	}{
		{
			name:                    "local",
			securityGroupName:       LocalSecurityGroup,
			expectedSecurityGroupID: "",
		},

		{
			name:                    "unspecified",
			securityGroupName:       "",
			expectedSecurityGroupID: "",
		},

		{
			name:                    "any",
			securityGroupName:       AnySecurityGroup,
			expectedSecurityGroupID: "-1",
		},

		{
			name: "name of existing security group",
			fqNameToSG: map[string]*SecurityGroup{
				"default-domain:project-blue:default": {
					SecurityGroupID: 8000002,
				},
				"default-domain:project-blue:other": {
					SecurityGroupID: 8000003,
				},
			},
			securityGroupName:       "default-domain:project-blue:default",
			expectedSecurityGroupID: "8000002",
		},

		{
			name:              "unknown security group name",
			securityGroupName: "some:unknown:security-group",
			fails:             true,
		},
	}

	for _, tt := range testCases {
		t.Run(tt.name, func(t *testing.T) {
			securityGroupID, err := securityGroupNameToID(tt.securityGroupName, tt.fqNameToSG)
			if tt.fails {
				assert.Error(t, err)
			} else {
				assert.NoError(t, err)
			}
			assert.Equal(t, tt.expectedSecurityGroupID, securityGroupID)
		})
	}
}
