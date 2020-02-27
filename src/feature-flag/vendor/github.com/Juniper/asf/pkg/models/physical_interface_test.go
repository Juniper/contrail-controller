package models

import (
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestValidateESIFormat(t *testing.T) {
	tests := []struct {
		name  string
		esi   string
		fails bool
	}{
		{
			name: "empty string",
		},
		{
			name:  "invalid string",
			esi:   "0123456789abcdef0123456789abc",
			fails: true,
		},
		{
			name: "valid string with lower case",
			esi:  "01:23:45:67:89:ab:cd:ef:01:23",
		},
		{
			name: "valid string with uppercase",
			esi:  "FF:EE:DD:CC:BB:AA:99:88:77:66",
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			pi := new(PhysicalInterface)
			pi.EthernetSegmentIdentifier = tt.esi
			err := pi.ValidateESIFormat()
			if tt.fails {
				assert.Error(t, err)
				return
			}
			assert.NoError(t, err)
		})
	}
}
