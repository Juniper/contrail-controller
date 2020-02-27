package models

import (
	"testing"

	"github.com/stretchr/testify/assert"
)

func Test_uuidToMac(t *testing.T) {
	tests := []struct {
		name    string
		uuid    string
		wantMac string
		fails   bool
	}{
		{name: "empty", fails: true},
		{name: "numbers", uuid: "01234567890", wantMac: "02:01:23:45:67:90"},
		{name: "hex numbers", uuid: "0123456789A", wantMac: "02:01:23:45:67:9A"},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			gotMac, err := uuidToMac(tt.uuid)

			if tt.fails {
				assert.Error(t, err)
			} else {
				assert.NoError(t, err)
				assert.Equal(t, tt.wantMac, gotMac)
			}
		})
	}
}
