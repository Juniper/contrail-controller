package models

import (
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestIsReservedQFXVlanTag(t *testing.T) {
	tests := []struct {
		name                    string
		logicalInterfaceType    string
		logicalInterfaceVlanTag int64
		is                      bool
	}{
		{
			name: "empty type",
		},
		{
			name:                    "reserved vlan tag",
			logicalInterfaceType:    "L2",
			logicalInterfaceVlanTag: 4094,
			is:                      true,
		},
		{
			name:                    "non reserved vlan tag",
			logicalInterfaceType:    "L1",
			logicalInterfaceVlanTag: 4094,
		},
		{
			name:                    "non reserved vlan tag",
			logicalInterfaceType:    "L2",
			logicalInterfaceVlanTag: 1024,
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			li := new(LogicalInterface)
			li.LogicalInterfaceType = tt.logicalInterfaceType
			li.LogicalInterfaceVlanTag = tt.logicalInterfaceVlanTag
			assert.Equal(t, tt.is, li.IsReservedQFXVlanTag())
		})
	}
}
