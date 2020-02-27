package models

import (
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestCheckSubnetsOverlap(t *testing.T) {
	tests := []struct {
		name     string
		cidr1    string
		cidr2    string
		expected bool
		fails    bool
	}{
		{
			name:     "Subnets don't overlap",
			cidr1:    "10.0.0.0/24",
			cidr2:    "11.0.0.0/24",
			expected: false,
		},
		{
			name:     "Subnets don't overlap with different masks",
			cidr1:    "10.0.0.0/24",
			cidr2:    "11.0.0.0/16",
			expected: false,
		},
		{
			name:     "Same subnet",
			cidr1:    "10.0.0.0/24",
			cidr2:    "10.0.0.0/24",
			expected: true,
		},
		{
			name:     "Subnets overlap",
			cidr1:    "10.0.0.0/24",
			cidr2:    "10.0.0.0/16",
			expected: true,
		},
		{
			name:  "Subnet is invalid",
			cidr1: "10.0.0..0/24",
			cidr2: "10.0.0.0/16",
			fails: true,
		},
		{
			name:  "Subnet is invalid v2",
			cidr1: "10.0.0.666/24",
			cidr2: "10.0.0.0/16",
			fails: true,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			result, err := CheckSubnetsOverlap(tt.cidr1, tt.cidr2)

			if tt.fails == false {
				assert.NoError(t, err)
			} else {
				assert.Error(t, err)
			}
			assert.Equal(t, result, tt.expected)
		})
	}
}
