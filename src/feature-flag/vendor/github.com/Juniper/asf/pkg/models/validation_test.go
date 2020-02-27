package models

import (
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestFormatValidation(t *testing.T) {
	tests := []struct {
		name       string
		testString string
		format     string
		fails      bool
	}{
		{
			name:       "Empty string - fail",
			testString: "",
			format:     "date-time",
			fails:      true,
		},
		{
			name:       "Valid date time",
			testString: "2018-05-23T17:29:57.397227",
			format:     "date-time",
			fails:      false,
		},
		{
			name:       "Invalid date time",
			testString: "2018:05-23T17:29:57.397227",
			format:     "date-time",
			fails:      true,
		},
		{
			name:       "Hostname simple - pass",
			testString: "localhost",
			format:     "hostname",
			fails:      false,
		},
		{
			name:       "Hostname - pass",
			testString: "www.example.com",
			format:     "hostname",
			fails:      false,
		},
		{
			name:       "Hostname double dot at the end - fail",
			testString: "localhost..",
			format:     "hostname",
			fails:      true,
		},
		{
			name:       "Hostname - fail",
			testString: "12,12",
			format:     "hostname",
			fails:      true,
		},
		{
			name:       "Valid ipv4 address",
			testString: "10.1.1.10",
			format:     "ipv4",
			fails:      false,
		},
		{
			name:       "Invalid ipv4 address",
			testString: "10.1.1.256",
			format:     "ipv4",
			fails:      true,
		},
		{
			name:       "Invalid ipv4 address format",
			testString: "10.1.1-10",
			format:     "ipv4",
			fails:      true,
		},
		{
			name:       "Valid ipv6 address - fail",
			testString: "0:0:0:0:0:0:0:1",
			format:     "ipv4",
			fails:      true,
		},
		{
			name:       "Valid base64 encoded string",
			testString: "VmFsaWQgc3RyaW5nCg==",
			format:     "base64",
			fails:      false,
		},
		{
			name:       "Invalid base64 encoded string",
			testString: "Invalid string",
			format:     "base64",
			fails:      true,
		},
	}

	tv, err := NewTypeValidatorWithFormat()

	assert.NoError(t, err)

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			fv, err := tv.GetFormatValidator(tt.format)
			assert.NoError(t, err)
			err = fv(tt.testString)
			if tt.fails {
				assert.Error(t, err)
			} else {
				assert.NoError(t, err)
			}
		})
	}
}

func TestValidateAllowedAddressPair(t *testing.T) {
	tests := []struct {
		name               string
		allowedAddressPair *AllowedAddressPair
		fails              bool
	}{
		{
			name:               "No attributes - pass",
			allowedAddressPair: &AllowedAddressPair{},
			fails:              false,
		},
		{
			name: "IPv6 - pass",
			allowedAddressPair: &AllowedAddressPair{
				IP: &SubnetType{
					IPPrefix:    "2001:0db8:85a3:0000:0000:8a2e:0370:7334",
					IPPrefixLen: 120,
				},
				AddressMode: "active-standby",
			},
			fails: false,
		},
		{
			name: "IPv6 - too short",
			allowedAddressPair: &AllowedAddressPair{
				IP: &SubnetType{
					IPPrefix:    "2001:0db8:85a3:0000:0000:8a2e:0370:7334",
					IPPrefixLen: 24,
				},
				AddressMode: "active-standby",
			},
			fails: true,
		},
		{
			name: "IPv4 - pass",
			allowedAddressPair: &AllowedAddressPair{
				IP: &SubnetType{
					IPPrefix:    "192.168.0.1",
					IPPrefixLen: 24,
				},
				AddressMode: "active-standby",
			},
			fails: false,
		},
		{
			name: "IPv4 - too short",
			allowedAddressPair: &AllowedAddressPair{
				IP: &SubnetType{
					IPPrefix:    "192.168.0.1",
					IPPrefixLen: 23,
				},
				AddressMode: "active-standby",
			},
			fails: true,
		},
	}

	tv, err := NewTypeValidatorWithFormat()

	assert.NoError(t, err)

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			err := tv.ValidateAllowedAddressPair(tt.allowedAddressPair)
			if tt.fails {
				assert.Error(t, err)
			} else {
				assert.NoError(t, err)
			}
		})
	}
}

func TestValidateCommunityAttributes(t *testing.T) {
	tests := []struct {
		name                string
		communityAttributes *CommunityAttributes
		fails               bool
	}{
		{
			name:                "No attributes - pass",
			communityAttributes: &CommunityAttributes{},
			fails:               false,
		},
		{
			name: "Enum value - pass",
			communityAttributes: &CommunityAttributes{
				CommunityAttribute: []string{"no-export"},
			},
			fails: false,
		},
		{
			name: "Invalid regex - fail",
			communityAttributes: &CommunityAttributes{
				CommunityAttribute: []string{"00;00"},
			},
			fails: true,
		},
		{
			name: "Regex, value out of bounds - fail",
			communityAttributes: &CommunityAttributes{
				CommunityAttribute: []string{"66666:00"},
			},
			fails: true,
		},
		{
			name: "Valid regex - pass",
			communityAttributes: &CommunityAttributes{
				CommunityAttribute: []string{"65535:00"},
			},
			fails: false,
		},
	}

	tv, err := NewTypeValidatorWithFormat()

	assert.NoError(t, err)

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			err := tv.ValidateCommunityAttributes(tt.communityAttributes)
			if tt.fails {
				assert.Error(t, err)
			} else {
				assert.NoError(t, err)
			}
		})
	}
}

func TestServiceInterfaceTypeValidation(t *testing.T) {
	tests := []struct {
		name                string
		serviceInterfaceTag *ServiceInterfaceTag
		fails               bool
	}{
		{
			name: "Bad standard value - validation fails",
			serviceInterfaceTag: &ServiceInterfaceTag{
				InterfaceType: "hogehoge",
			},
			fails: true,
		},
		{
			name: "Bad regex value - validation fails",
			serviceInterfaceTag: &ServiceInterfaceTag{
				InterfaceType: "othe",
			},
			fails: true,
		},
		{
			name: "Standard value",
			serviceInterfaceTag: &ServiceInterfaceTag{
				InterfaceType: "management",
			},
			fails: false,
		},
		{
			name: "Regex value",
			serviceInterfaceTag: &ServiceInterfaceTag{
				InterfaceType: "other0",
			},
			fails: false,
		},
	}

	tv, err := NewTypeValidatorWithFormat()

	assert.NoError(t, err)

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			err := tv.ValidateServiceInterfaceTag(tt.serviceInterfaceTag)
			if tt.fails {
				assert.Error(t, err)
			} else {
				assert.NoError(t, err)
			}
		})
	}
}

func TestSchemaValidationJobTemplate(t *testing.T) {
	tests := []struct {
		name        string
		jobTemplate *JobTemplate
		fails       bool
	}{
		{
			name:        "Empty struct - validation fails",
			jobTemplate: &JobTemplate{},
			fails:       true,
		},
		{
			name: "Validation pass",
			jobTemplate: &JobTemplate{
				UUID:                        "beef",
				ParentUUID:                  "beef-beef",
				ParentType:                  "global-system-config",
				Name:                        "default-job-template",
				JobTemplatePlaybooks:        &PlaybookInfoListType{},
				JobTemplateConcurrencyLevel: "fabric",
				FQName:                      []string{"a", "b"},
			},
		},
		{
			name: "Bad parent type",
			jobTemplate: &JobTemplate{
				UUID:                        "beef",
				ParentUUID:                  "beef-beef",
				ParentType:                  "hogehoge",
				Name:                        "default-job-template",
				JobTemplatePlaybooks:        &PlaybookInfoListType{},
				JobTemplateConcurrencyLevel: "fabric",
				FQName:                      []string{"a", "b"},
			},
			fails: true,
		},
	}

	tv, err := NewTypeValidatorWithFormat()

	assert.NoError(t, err)

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			err := tv.ValidateJobTemplate(tt.jobTemplate)
			if tt.fails {
				assert.Error(t, err)
			} else {
				assert.NoError(t, err)
			}
		})
	}
}

func TestSchemaValidationBgpFamilyAttributes(t *testing.T) {
	tests := []struct {
		name                string
		bgpFamilyAttributes *BgpFamilyAttributes
		fails               bool
	}{
		{
			name: "Validation pass",
			bgpFamilyAttributes: &BgpFamilyAttributes{
				AddressFamily: "inet",
				LoopCount:     0,
			},
			fails: false,
		},
		{
			name: "Bad string value",
			bgpFamilyAttributes: &BgpFamilyAttributes{
				AddressFamily: "hogehoge",
			},
			fails: true,
		},
	}

	tv, err := NewTypeValidatorWithFormat()

	assert.NoError(t, err)

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			err := tv.ValidateBgpFamilyAttributes(tt.bgpFamilyAttributes)
			if tt.fails {
				assert.Error(t, err)
			} else {
				assert.NoError(t, err)
			}
		})
	}
}

func TestSchemaValidationStaticMirrorNhType(t *testing.T) {
	tests := []struct {
		name               string
		staticMirrorNhType *StaticMirrorNhType
		fails              bool
	}{
		{
			name: "Pass",
			staticMirrorNhType: &StaticMirrorNhType{
				VtepDSTIPAddress:  "hoge",
				VtepDSTMacAddress: "hoge",
				Vni:               1,
			},
			fails: false,
		},
	}

	tv, err := NewTypeValidatorWithFormat()

	assert.NoError(t, err)

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			err := tv.ValidateStaticMirrorNhType(tt.staticMirrorNhType)
			if tt.fails {
				assert.Error(t, err)
			} else {
				assert.NoError(t, err)
			}
		})
	}
}
