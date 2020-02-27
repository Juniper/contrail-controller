package models

import (
	"github.com/Juniper/asf/pkg/errutil"

	uuid "github.com/satori/go.uuid"
)

// CheckNetworkPolicyRules validates policy rules from policy entries for Network Policy.
func (e *PolicyEntriesType) CheckNetworkPolicyRules() error {
	if e == nil {
		return nil
	}

	rules := e.GetPolicyRule()

	if err := checkPolicyEntriesRules(rules); err != nil {
		return err
	}

	for _, rule := range rules {
		if rule.ActionList == nil {
			return errutil.ErrorBadRequest("Check Policy Rules failed. Action is required.")
		}

		if rule.HasSecurityGroup() {
			return errutil.ErrorBadRequest("Config Error: Policy Rule refering to Security Group is not allowed")
		}
	}

	return nil
}

// CheckSecurityGroupRules validates policy rules from policy entries for Security Group.
func (e *PolicyEntriesType) CheckSecurityGroupRules() error {
	if e == nil {
		return nil
	}

	rules := e.GetPolicyRule()

	if err := checkPolicyEntriesRules(rules); err != nil {
		return err
	}

	for _, rule := range rules {
		if err := rule.ValidateSubnetsWithEthertype(); err != nil {
			return err
		}
		if !rule.IsAnySecurityGroupAddrLocal() {
			return errutil.ErrorBadRequest("At least one of source " +
				"or destination addresses must be 'local'")
		}
	}

	return nil
}

// FillRuleUUIDs adds UUID to every PolicyRule within PolicyEntriesType
// which doesn't have one.
func (e *PolicyEntriesType) FillRuleUUIDs() {
	if e == nil {
		return
	}

	for i, rule := range e.PolicyRule {
		if rule.GetRuleUUID() == "" {
			e.PolicyRule[i].RuleUUID = uuid.NewV4().String()
		}
	}
}

func checkPolicyEntriesRules(rules []*PolicyRuleType) error {
	for i, rule := range rules {
		remainingRules := rules[i+1:]
		if isRuleInRules(rule, remainingRules) {
			return errutil.ErrorConflictf("Rule already exists: %v", rule.GetRuleUUID())
		}

		if err := rule.ValidateProtocol(); err != nil {
			return err
		}
	}
	return nil
}

func isRuleInRules(rule *PolicyRuleType, rules []*PolicyRuleType) bool {
	for _, r := range rules {
		if r.EqualRule(*rule) {
			return true
		}
	}
	return false
}
