package models

import (
	"fmt"
	"strings"

	"github.com/Juniper/asf/pkg/models/basemodels"
)

// TagTypeValueFromFQName extracts type and value from tag's fqName.
func TagTypeValueFromFQName(fqName []string) (tagType, tagValue string) {
	return TagTypeValueFromName(basemodels.FQNameToName(fqName))
}

// TagTypeValueFromName extracts type and value from tag's name.
func TagTypeValueFromName(name string) (tagType, tagValue string) {
	splits := strings.Split(name, "=")
	if len(splits) < 2 {
		return "", ""
	}
	return splits[0], splits[1]
}

// CreateTagName constructs tag name from it's type and value.
func CreateTagName(tagType, tagValue string) string {
	return fmt.Sprintf("%v=%v", tagType, tagValue)
}
