package testutil

import (
	"testing"

	"github.com/stretchr/testify/assert"
	yaml "gopkg.in/yaml.v2"
)

var actualYAML = `
string: value
string2: "1.0"
list:
- element1
- element2
- element3
map:
  key1: value1
  key2: value2
number: 1
uuid: 770b8ebe-decb-47e8-89a4-33eb0cef28b3
bool: true
nilValue: null
float: 1.0
uint: 18446744073709551615
`

var test1YAML = `
string: value
string2: "1.0"
list:
- element1
- element2
- element3
map:
  key1: value1
number: 1
bool: true
float: 1.0
uint: 18446744073709551615
`

var test2YAML = `
string: value
list:
- element1
- $any
- $any
map:
  key1: $any
number: $number
uuid: $uuid
bool: true
nilValue: $null
`

var missingKeys = `
string: value
`

var missingKeysInNestedMap = `
map: {}
`

var nullMapKey = `
map: null
`

func TestAssertEquals(t *testing.T) {
	tests := []struct {
		name     string
		expected string
		actual   string
	}{
		{name: "test1", expected: test1YAML, actual: actualYAML},
		{name: "test2", expected: test2YAML, actual: actualYAML},
		{name: "missing keys", expected: missingKeys, actual: actualYAML},
		{name: "missing keys in nested map", expected: missingKeysInNestedMap, actual: actualYAML},
		{name: "null key", expected: nullMapKey, actual: actualYAML},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			var actualData interface{}
			err := yaml.Unmarshal([]byte(tt.actual), &actualData)
			assert.NoError(t, err)

			var testData interface{}
			err = yaml.Unmarshal([]byte(tt.expected), &testData)
			assert.NoError(t, err)

			AssertEqual(t, testData, actualData, "check same data: %s")
		})
	}
}
