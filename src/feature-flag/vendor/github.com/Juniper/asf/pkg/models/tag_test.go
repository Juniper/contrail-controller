package models

import (
	"testing"

	"github.com/stretchr/testify/assert"

	"github.com/Juniper/asf/pkg/models/basemodels"
)

func TestTagTypeValueFromTagFQName(t *testing.T) {
	tests := []struct {
		fqName       []string
		wantTagType  string
		wantTagValue string
	}{
		{[]string{}, "", ""},
		{[]string{""}, "", ""},
		{[]string{"foo=bar"}, "foo", "bar"},
		{[]string{"grandparent", "parent", "foo=bar"}, "foo", "bar"},
	}
	for _, tt := range tests {
		t.Run(basemodels.FQNameToString(tt.fqName), func(t *testing.T) {
			gotTagType, gotTagValue := TagTypeValueFromFQName(tt.fqName)
			assert.Equal(t, tt.wantTagType, gotTagType)
			assert.Equal(t, tt.wantTagValue, gotTagValue)
		})
	}
}

func TestTagTypeValueFromName(t *testing.T) {
	tests := []struct {
		name         string
		wantTagType  string
		wantTagValue string
	}{
		{"", "", ""},
		{"foo=bar", "foo", "bar"},
		{"foo", "", ""},
		{"foo=asd=bar", "foo", "asd"},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			gotTagType, gotTagValue := TagTypeValueFromName(tt.name)
			assert.Equal(t, tt.wantTagType, gotTagType)
			assert.Equal(t, tt.wantTagValue, gotTagValue)
		})
	}
}
