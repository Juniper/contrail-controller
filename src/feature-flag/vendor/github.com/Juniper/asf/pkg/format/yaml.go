package format

import (
	yaml "gopkg.in/yaml.v2"
)

// MustYAML returns YAML-encoded data.
func MustYAML(data interface{}) string {
	b, err := yaml.Marshal(data)
	if err != nil {
		return ""
	}
	return string(b)
}
