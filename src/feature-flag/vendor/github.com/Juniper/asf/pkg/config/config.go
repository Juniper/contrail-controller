package config

import (
	"github.com/spf13/viper"
	yaml "gopkg.in/yaml.v2"
)

//LoadConfig load data from data and bind to struct.
func LoadConfig(path string, dest interface{}) error {
	config := viper.Get(path)
	configYAML, err := yaml.Marshal(config)
	if err != nil {
		return err
	}

	return yaml.UnmarshalStrict(configYAML, dest)
}
