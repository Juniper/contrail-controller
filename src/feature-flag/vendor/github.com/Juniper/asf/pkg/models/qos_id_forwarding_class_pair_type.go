package models

import (
	"github.com/Juniper/asf/pkg/errutil"
)

// CheckKeyDscp checks if QosIdForwardingClassPair as DSCP key is valid.
func (m *QosIdForwardingClassPair) CheckKeyDscp() error {
	if m != nil && (m.GetKey() < 0 || m.GetKey() > 63) {
		return errutil.ErrorBadRequestf("Invalid DSCP value %v.", m.GetKey())
	}
	return nil
}

// CheckKeyVlanPriority checks if QosIdForwardingClassPair as Vlan Priority key is valid.
func (m *QosIdForwardingClassPair) CheckKeyVlanPriority() error {
	if m != nil && (m.GetKey() < 0 || m.GetKey() > 7) {
		return errutil.ErrorBadRequestf("Invalid 802.1p value %v.", m.GetKey())
	}
	return nil
}

// CheckKeyMplsExp checks if QosIdForwardingClassPair as MPLS EXP key is valid.
func (m *QosIdForwardingClassPair) CheckKeyMplsExp() error {
	if m != nil && (m.GetKey() < 0 || m.GetKey() > 7) {
		return errutil.ErrorBadRequestf("Invalid MPLS EXP value %v.", m.GetKey())
	}
	return nil
}
