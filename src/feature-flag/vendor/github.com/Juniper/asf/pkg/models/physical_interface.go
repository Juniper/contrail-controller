package models

import (
	"regexp"

	"github.com/Juniper/asf/pkg/errutil"
)

var regexpPhysicalInterfaceESIFormat = regexp.MustCompile("^([0-9A-Fa-f]{2}[:]){9}[0-9A-Fa-f]{2}")

// ValidateESIFormat validates ESI string format
func (m *PhysicalInterface) ValidateESIFormat() error {
	if len(m.EthernetSegmentIdentifier) != 0 &&
		!regexpPhysicalInterfaceESIFormat.MatchString(m.EthernetSegmentIdentifier) {
		return errutil.ErrorBadRequestf("invalid esi string format %s", m.EthernetSegmentIdentifier)
	}
	return nil
}
