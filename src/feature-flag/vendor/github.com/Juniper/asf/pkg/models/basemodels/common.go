package basemodels

import (
	"fmt"
	"strings"
	"time"
)

// CommonFieldPerms2 is a resource field that stores PermType2 data.
const CommonFieldPerms2 = "perms2"

const (
	//PermsNone for no permission
	PermsNone = iota
	//PermsX for exec permission
	PermsX
	//PermsW for write permission
	PermsW
	//PermsWX for exec and write permission
	PermsWX
	//PermsR for read permission
	PermsR
	//PermsRX for exec and read permission
	PermsRX
	//PermsRW for read and write permission
	PermsRW
	//PermsRWX for all permission
	PermsRWX
)

// ParseFQName parse string representation of FQName.
func ParseFQName(fqNameString string) []string {
	if fqNameString == "" {
		return nil
	}
	return strings.Split(fqNameString, ":")
}

// FQNameToString returns string representation of FQName.
func FQNameToString(fqName []string) string {
	return strings.Join(fqName, ":")
}

// DefaultNameForKind constructs the default name for an object of the given kind.
func DefaultNameForKind(kind string) string {
	return fmt.Sprintf("default-%s", kind)
}

// ParentFQName gets parent fqName.
func ParentFQName(fqName []string) []string {
	if len(fqName) > 1 {
		return fqName[:len(fqName)-1]
	}
	return []string{}
}

// ChildFQName constructs fqName for child.
func ChildFQName(parentFQName []string, childName string) []string {
	result := make([]string, 0, len(parentFQName)+1)
	result = append(result, parentFQName...)
	if childName != "" {
		result = append(result, childName)
	}
	return result
}

// FQNameToName gets object's name from it's fqName.
func FQNameToName(fqName []string) string {
	pos := len(fqName) - 1
	if pos < 0 {
		return ""
	}
	return fqName[pos]
}

// FQNameEquals checks if fqName slices have the same length and values
func FQNameEquals(fqNameA, fqNameB []string) bool {
	if len(fqNameA) != len(fqNameB) {
		return false
	}
	for i, v := range fqNameA {
		if v != fqNameB[i] {
			return false
		}
	}
	return true
}

// KindToSchemaID makes a snake_case schema ID from a kebab-case kind.
func KindToSchemaID(kind string) string {
	return strings.Replace(kind, "-", "_", -1)
}

// SchemaIDToKind makes a kebab-case kind from a snake_case schema ID.
func SchemaIDToKind(schemaID string) string {
	return strings.Replace(schemaID, "_", "-", -1)
}

// ReferenceKind constructs reference kind for given from and to kinds.
func ReferenceKind(fromKind, toKind string) string {
	return fmt.Sprintf("%s-%s", fromKind, toKind)
}

// OmitEmpty removes map field that should be removed if empty.
func OmitEmpty(m map[string]interface{}) {
	for _, key := range []string{"parent_type", "parent_uuid"} {
		if v, ok := m[key]; ok && isEmpty(v) {
			delete(m, key)
		}
	}
}

func isEmpty(i interface{}) bool {
	switch v := i.(type) {
	case string:
		return v == ""
	case int:
		return v == 0
	}
	return false
}

const (
	vncFormatWithoutNanoseconds = "2006-01-02T15:04:05"
	vncFormatWithNanoseconds    = "2006-01-02T15:04:05.000000"
)

// ToVNCTime returns time string in VNC format.
func ToVNCTime(t time.Time) string {
	var format string
	if t.Nanosecond() < 1000 {
		format = vncFormatWithoutNanoseconds
	} else {
		format = vncFormatWithNanoseconds
	}
	return t.UTC().Format(format)
}
