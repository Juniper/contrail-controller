package models

import (
	"github.com/Juniper/asf/pkg/errutil"
)

// ValidateEndpointType checks if endpoint refers to only one endpoint type
func (e *FirewallRuleEndpointType) ValidateEndpointType() error {
	if e == nil {
		return nil
	}

	//TODO update should check with database endpoint values that are not in a request
	count := 0
	if e.GetAddressGroup() != "" {
		count++
	}
	if e.GetAny() == true {
		count++
	}
	if e.GetSubnet() != nil {
		count++
	}
	if len(e.GetTags()) > 0 {
		count++
	}
	if e.GetVirtualNetwork() != "" {
		count++
	}

	if count > 1 {
		return errutil.ErrorBadRequest("endpoint is limited to only one endpoint type at a time")
	}

	return nil
}
