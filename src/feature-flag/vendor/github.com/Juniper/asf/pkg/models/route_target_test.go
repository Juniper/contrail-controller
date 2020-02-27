package models

import (
	"net"
	"testing"

	"github.com/stretchr/testify/assert"
)

type parsedRT struct {
	ip     net.IP
	asn    int
	target int
}

func TestParseRouteTarget(t *testing.T) {

	tests := []struct {
		name           string
		routeTarget    string
		expectedResult *parsedRT
		fails          bool
	}{
		{
			name:        "Try to parse route target with ip in name",
			routeTarget: "target:10.0.0.0:8000000",
			expectedResult: &parsedRT{
				ip:     net.ParseIP("10.0.0.0"),
				target: 8000000,
			},
			fails: false,
		},
		{
			name:        "Try to parse route target with asn in name",
			routeTarget: "target:123456:5123",
			expectedResult: &parsedRT{
				asn:    123456,
				target: 5123,
			},
			fails: false,
		},
		{
			name:        "Try to parse route target with wrong prefix",
			routeTarget: "rt:10.0.0.0:8000000",
			fails:       true,
		},
		{
			name:        "Try to parse route target without ip/asn",
			routeTarget: "target::8000000",
			fails:       true,
		},
		{
			name:        "Try to parse route target with wrong ip/asn",
			routeTarget: "target:512.54:8000000",
			fails:       true,
		},
		{
			name:        "Try to parse route target with int out of range as ip/asn",
			routeTarget: "target:10.0.0.0:555555555555555555555555",
			fails:       true,
		},
		{
			name:        "Try to parse route target with int out of range as target",
			routeTarget: "target:555555555555555555555555:5123",
			fails:       true,
		},
		{
			name:        "Try to parse route target without ip/asn and with wrong target",
			routeTarget: "target:::",
			fails:       true,
		},
		{
			name:        "Try to parse route target without fields",
			routeTarget: "::",
			fails:       true,
		},
	}

	for _, tt := range tests {
		ip, asn, target, err := parseRouteTarget(tt.routeTarget)
		if tt.fails {
			assert.Error(t, err)
		} else {
			assert.NoError(t, err)
			assert.Equal(t, tt.expectedResult.ip, ip)
			assert.Equal(t, tt.expectedResult.asn, asn)
			assert.Equal(t, tt.expectedResult.target, target)
		}
	}
}
