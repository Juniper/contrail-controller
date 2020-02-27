package basedb

import (
	"encoding/json"

	"github.com/pkg/errors"
)

// ParseFQName parses fqName string read from DB to string slice
func ParseFQName(fqNameStr string) ([]string, error) {
	var fqName []string
	err := json.Unmarshal([]byte(fqNameStr), &fqName)
	if err != nil {
		return nil, errors.Errorf("failed to parse fq name from string: %v", err)
	}
	return fqName, nil
}

func fqNameToString(fqName []string) (string, error) {
	fqNameStr, err := json.Marshal(fqName)
	if err != nil {
		return "", errors.Errorf("failed to parse fq name to string: %v", err)
	}
	return string(fqNameStr), nil
}
