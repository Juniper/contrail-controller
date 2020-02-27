package models

import (
	"math"

	"github.com/pkg/errors"
)

// BGP constants.
const (
	DefaultPortRangeStart = 50000
	DefaultPortRangeEnd   = 50512
)

// ValidatePortRange checks if ports specified are valid and if start < end.
func (ports *BGPaaServiceParametersType) ValidatePortRange() error {

	if (ports.PortStart > ports.PortEnd) || (ports.PortStart <= 0) || (ports.PortEnd > math.MaxUint16) {
		return errors.Errorf("Invalid Port range specified (%d : %d)", ports.PortStart, ports.PortEnd)
	}
	return nil
}

// GetDefaultBGPaaServiceParameters creates BGPaaService parameters with a default port range.
func GetDefaultBGPaaServiceParameters() *BGPaaServiceParametersType {
	return &BGPaaServiceParametersType{PortStart: DefaultPortRangeStart, PortEnd: DefaultPortRangeEnd}
}

// EnclosesRange checks if port range includes the other one.
func (ports *BGPaaServiceParametersType) EnclosesRange(other *BGPaaServiceParametersType) bool {
	if ports != nil && other != nil {
		return ports.PortStart <= other.PortStart && ports.PortEnd >= other.PortEnd
	}
	return false
}
