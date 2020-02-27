package models

import (
	"strconv"

	"github.com/pkg/errors"
	"github.com/sirupsen/logrus"
)

// PolicyRulesWithRefs is a list of policy rules together with resolved references
// to security groups whose names are mentioned in the rules.
type PolicyRulesWithRefs struct {
	Rules      []*PolicyRuleType
	FQNameToSG map[string]*SecurityGroup
}

// ToACLRules translates policy rules to ACL rules.
func (rs *PolicyRulesWithRefs) ToACLRules() (ingressRules, egressRules []*AclRuleType) {
	for _, pair := range allAddressCombinations(rs.Rules) {
		rule, err := makeACLRule(pair, rs.FQNameToSG)
		if err != nil {
			logrus.WithError(err).Error("Ignoring ACL rule")
			continue
		}

		isIngress, err := pair.IsIngress()
		if err != nil {
			logrus.WithError(err).Error("Ignoring ACL rule")
			continue
		}

		if isIngress {
			ingressRules = append(ingressRules, rule)
		} else {
			egressRules = append(egressRules, rule)
		}
	}
	return ingressRules, egressRules
}

// MakeChildACL returns a child ACL for a security group with the given ACL rules.
func (m *SecurityGroup) MakeChildACL(name string, rules []*AclRuleType) *AccessControlList {
	return &AccessControlList{
		Name:       name,
		ParentType: m.Kind(),
		ParentUUID: m.GetUUID(),
		AccessControlListEntries: &AclEntriesType{
			ACLRule: rules,
		},
	}
}

func allAddressCombinations(rs []*PolicyRuleType) (pairs []PolicyAddressPair) {
	for _, r := range rs {
		pairs = append(pairs, r.allAddressCombinations()...)
	}
	return pairs
}

func makeACLRule(pair PolicyAddressPair, fqNameToSG map[string]*SecurityGroup) (*AclRuleType, error) {
	protocol, err := pair.PolicyRule.ACLProtocol()
	if err != nil {
		return nil, err
	}

	sourceAddress, err := addressTypeToACLAddress(pair.SourceAddress, fqNameToSG)
	if err != nil {
		return nil, errors.Wrap(err, "failed to convert source address for an ACL")
	}
	destinationAddress, err := addressTypeToACLAddress(pair.DestinationAddress, fqNameToSG)
	if err != nil {
		return nil, errors.Wrap(err, "failed to convert destination address for an ACL")
	}

	return &AclRuleType{
		RuleUUID: pair.PolicyRule.GetRuleUUID(),
		MatchCondition: &MatchConditionType{
			Ethertype:  pair.PolicyRule.GetEthertype(),
			Protocol:   protocol,
			SRCAddress: sourceAddress,
			DSTAddress: destinationAddress,
			SRCPort:    pair.SourcePort,
			DSTPort:    pair.DestinationPort,
		},
		ActionList: &ActionListType{
			SimpleAction: "pass",
		},
	}, nil
}

func addressTypeToACLAddress(
	addr *AddressType, fqNameToSG map[string]*SecurityGroup) (*AddressType, error) {

	numericSecurityGroup, err := securityGroupNameToID(addr.SecurityGroup, fqNameToSG)
	if err != nil {
		return nil, errors.Wrap(err, "failed to convert security group name for an ACL")
	}

	aclAddress := *addr
	aclAddress.SecurityGroup = numericSecurityGroup
	return &aclAddress, nil
}

func securityGroupNameToID(name string, fqNameToSG map[string]*SecurityGroup) (string, error) {
	switch {
	case name == LocalSecurityGroup || name == UnspecifiedSecurityGroup:
		return "", nil
	case name == AnySecurityGroup:
		return "-1", nil
	default:
		sg := fqNameToSG[name]
		if sg == nil {
			return "", errors.Errorf("unknown security group name %q", name)
		}
		return strconv.FormatInt(sg.GetSecurityGroupID(), 10), nil
	}
}
