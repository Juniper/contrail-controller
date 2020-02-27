package testutil

import (
	"fmt"
	"net"
	"strconv"
	"strings"
	"testing"
	"time"

	"github.com/Juniper/asf/pkg/errutil"
	"github.com/Juniper/asf/pkg/fileutil"
	"github.com/Juniper/asf/pkg/format"
	"github.com/golang/mock/gomock"
	"github.com/pkg/errors"
	"github.com/stretchr/testify/assert"

	uuid "github.com/satori/go.uuid"
)

const (
	funcPrefix        = "$"
	iso8601TimeFormat = "2006-01-02T15:04:05" // ISO8601 time format without a timezone.
)

// NotNil matches any non-nil value.
func NotNil() gomock.Matcher {
	return gomock.Not(gomock.Nil())
}

// assertFunction for macros in diff.
type assertFunction func(path string, args, actual interface{}) error

// assertFunctions for additional test logic.
var assertFunctions = map[string]assertFunction{
	"any": func(path string, args, actual interface{}) error {
		return nil
	},
	"null": func(path string, _, actual interface{}) error {
		if actual != nil {
			return errors.Errorf("expected null but got %s on path %s", actual, path)
		}
		return nil
	},
	"number": func(path string, _, actual interface{}) error {
		switch actual.(type) {
		case int64, int, float64:
			return nil
		}
		return errors.Errorf("expected number but got %s on path %s", actual, path)
	},
	"uuid": func(path string, _, actual interface{}) error {
		if val, ok := actual.(string); ok {
			if _, err := uuid.FromString(val); err != nil {
				return errors.Errorf("expected uuid but got %s on path %s (error: %s)", actual, path, err)
			}
			return nil
		}
		return errors.Errorf("expected uuid string but got %s on path %s", actual, path)
	},
	"mac_address": func(path string, _, actual interface{}) error {
		if val, ok := actual.(string); ok {
			if _, err := net.ParseMAC(val); err != nil {
				return errors.Errorf("expected mac address but got %s on path %s (error: %s)", actual, path, err)
			}
			return nil
		}
		return errors.Errorf("expected mac address string string but got %s on path %s", actual, path)
	},
	"ip_address": func(path string, _, actual interface{}) error {
		if val, ok := actual.(string); ok {
			if ip := net.ParseIP(val); ip == nil {
				return errors.Errorf("expected ip address but got %s on path %s", actual, path)
			}
			return nil
		}
		return errors.Errorf("expected ip address string but got %s on path %s", actual, path)
	},
	"datetime_iso": func(path string, _, actual interface{}) error {
		if val, ok := actual.(string); ok {
			if _, err := time.Parse(iso8601TimeFormat, val); err != nil {
				return errors.Errorf("expected datetime stamp ISO8601 but got %s on path %s", actual, path)
			}
			return nil
		}
		return errors.Errorf("expected datetime stamp string but got %s on path %s", actual, path)
	},
	"datetime_RFC3339": func(path string, _, actual interface{}) error {
		if val, ok := actual.(string); ok {
			if _, err := time.Parse(time.RFC3339, val); err != nil {
				return errors.Errorf("expected datetime stamp RFC3339 but got %s on path %s", actual, path)
			}
			return nil
		}
		return errors.Errorf("expected datetime stamp string but got %s on path %s", actual, path)
	},
	"contains": func(path string, pattern, actual interface{}) error {
		val, ok := actual.(string)
		if !ok {
			return errors.Errorf("expected a string but got %T on path %s", actual, path)
		}
		substr, ok := pattern.(string)
		if !ok {
			return errors.Errorf("expected pattern to be a string but got %T on pattern arg %s", actual, path)
		}

		if strings.Contains(val, substr) {
			return nil
		}
		return errors.Errorf("expected value to contain substring '%s', but value is '%s' on path %s", substr, val, path)
	},
}

// AssertEqual asserts that expected and actual objects are equal, performing comparison recursively.
// For lists and maps, it iterates over expected values, ignoring additional values in actual object.
func AssertEqual(t *testing.T, expected, actual interface{}, msg ...string) bool {
	return assert.NoError(
		t,
		IsObjectSubsetOf(expected, actual),
		fmt.Sprintf(
			"%s: objects not equal:\nexpected:\n%+v\nactual:\n%+v",
			strings.Join(msg, ", "),
			format.MustYAML(expected),
			format.MustYAML(actual),
		),
	)
}

// IsObjectSubsetOf verifies if "subset" structure contains all fields described
// in "of" structure and throws an error in case if it doesn't.
func IsObjectSubsetOf(subset, of interface{}) error {
	return checkDiff(
		"",
		fileutil.YAMLtoJSONCompat(subset),
		fileutil.YAMLtoJSONCompat(of),
	)
}

// nolint: gocyclo
func checkDiff(path string, expected, actual interface{}) error {
	if expected == nil {
		return nil
	}
	if isFunction(expected) {
		return runFunction(path, expected, actual)
	}
	switch t := expected.(type) {
	case map[interface{}]interface{}:
		actualMap, ok := actual.(map[string]interface{})
		if !ok {
			return errorWithFields(t, actual, path)
		}
		for keyI, value := range t {
			key := fmt.Sprint(keyI)
			if err := checkDiff(fmt.Sprintf("%s.%s", path, key), value, actualMap[key]); err != nil {
				return err
			}
		}
	case map[string]interface{}:
		actualMap, ok := actual.(map[string]interface{})
		if !ok {
			return errorWithFields(t, actual, path)
		}
		for key, value := range t {
			if err := checkDiff(fmt.Sprintf("%s.%s", path, key), value, actualMap[key]); err != nil {
				return err
			}
		}
	case []interface{}:
		actualList, ok := actual.([]interface{})
		if !ok {
			return errorWithFields(t, actual, path)
		}
		if len(t) != len(actualList) {
			return errorWithFields(t, actual, path)
		}
		for i, value := range t {
			var mErr errutil.MultiError
			found := false
			for _, actualValue := range actualList {
				err := checkDiff(path+"."+strconv.Itoa(i), value, actualValue)
				if err == nil {
					found = true
					break
				}
				mErr = append(mErr, err)
			}
			if !found {
				return fmt.Errorf("%s not found, last err: %v", path+"."+strconv.Itoa(i), mErr)
			}
		}
	case int, int64:
		if format.InterfaceToInt64(t) != format.InterfaceToInt64(actual) {
			return errorWithFields(t, actual, path)
		}
	case uint, uint64:
		if format.InterfaceToUint64(t) != format.InterfaceToUint64(actual) {
			return errorWithFields(t, actual, path)
		}
	case float32, float64:
		if format.InterfaceToFloat(t) != format.InterfaceToFloat(actual) {
			return errorWithFields(t, actual, path)
		}
	default:
		if t != actual {
			return errorWithFields(t, actual, path)
		}
	}
	return nil
}

func isFunction(expected interface{}) bool {
	switch t := expected.(type) {
	case map[string]interface{}:
		for key := range t {
			if isStringFunction(key) {
				return true
			}
		}
	case string:
		return isStringFunction(t)
	}
	return false
}

func isStringFunction(key string) bool {
	return strings.HasPrefix(key, funcPrefix)
}

// nolint: gocyclo
func runFunction(path string, expected, actual interface{}) (err error) {
	switch t := expected.(type) {
	case map[string]interface{}:
		for key := range t {
			if isStringFunction(key) {
				for key, value := range t {
					checkFn, err := getAssertFunction(key)
					if err != nil {
						return err
					}
					err = checkFn(path, value, actual)
					if err != nil {
						return err
					}
				}
			}
		}
	case string:
		if isStringFunction(t) {
			assert, err := getAssertFunction(t)
			if err != nil {
				return err
			}
			err = assert(path, nil, actual)
			if err != nil {
				return err
			}
		}
	}
	return nil
}

func getAssertFunction(key string) (assertFunction, error) {
	assertName := strings.TrimPrefix(key, funcPrefix)
	assert, ok := assertFunctions[assertName]
	if !ok {
		return nil, fmt.Errorf("assert function %s not found", assertName)
	}
	return assert, nil
}

func errorWithFields(expected, actual interface{}, path string) error {
	return fmt.Errorf("expected(%T):\n%v\nactual(%T):\n%v\npath: %s",
		expected,
		format.MustYAML(expected),
		actual,
		format.MustYAML(actual),
		path)
}
