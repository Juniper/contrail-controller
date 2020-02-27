package format

import (
	"encoding/json"
	"fmt"
	"strconv"

	"github.com/pkg/errors"
	"github.com/sirupsen/logrus"
)

//InterfaceToInt makes an int from interface with error reporting.
func InterfaceToInt(i interface{}) int {
	ret, _ := InterfaceToIntE(i) // nolint:errcheck
	return ret
}

//InterfaceToIntE makes an int from interface with error reporting with error reporting.
// nolint: gocyclo
func InterfaceToIntE(i interface{}) (int, error) {
	if i == nil {
		return 0, nil
	}
	switch t := i.(type) {
	case []byte:
		n, err := strconv.Atoi(string(t))
		if err != nil {
			logrus.WithError(err).Debugf("could not convert %#v to int", t)
		}
		return n, err
	case string:
		n, err := strconv.Atoi(t)
		if err != nil {
			logrus.WithError(err).Debugf("could not convert %#v to int", t)
		}
		return n, err
	case int:
		return t, nil
	case int64:
		return int(t), nil
	case float64:
		return int(t), nil
	case json.Number:
		n64, err := t.Int64()
		if err != nil {
			logrus.WithError(err).Debugf("could not convert %#v to int", t)
		}
		return int(n64), err
	case nil:
		return 0, nil
	default:
		err := fmt.Errorf("could not convert %#v to int", i)
		logrus.Debug(err.Error())
		return 0, err
	}
}

//InterfaceToInt64 makes an int64 from interface.
func InterfaceToInt64(i interface{}) int64 {
	ret, _ := InterfaceToInt64E(i) // nolint:errcheck
	return ret
}

//InterfaceToInt64E makes an int64 from interface with error reporting with error reporting.
// nolint: gocyclo
func InterfaceToInt64E(i interface{}) (int64, error) {
	if i == nil {
		return 0, nil
	}
	switch t := i.(type) {
	case []byte:
		i64, err := strconv.ParseInt(string(t), 10, 64)
		if err != nil {
			logrus.WithError(err).Debugf("could not convert %#v to int64", t)
		}
		return i64, err
	case string:
		i64, err := strconv.ParseInt(t, 10, 64)
		if err != nil {
			logrus.WithError(err).Debugf("could not convert %#v to int64", t)
		}
		return i64, err
	case int:
		return int64(t), nil
	case int32:
		return int64(t), nil
	case int64:
		return t, nil
	case float64:
		return int64(t), nil
	case json.Number:
		i64, err := t.Int64()
		if err != nil {
			logrus.WithError(err).Debugf("could not convert %#v to int64", t)
		}
		return i64, err
	case nil:
		return 0, nil
	default:
		err := fmt.Errorf("could not convert (%T) %#v to int64", i, i)
		logrus.Debug(err.Error())
		return 0, err
	}
}

//InterfaceToUint64 makes a uint64 from interface.
func InterfaceToUint64(i interface{}) uint64 {
	var ret, _ = InterfaceToUint64E(i) // nolint:errcheck
	return ret
}

//InterfaceToUint64E makes a uint64 from interface with error reporting.
// nolint: gocyclo
func InterfaceToUint64E(i interface{}) (uint64, error) {
	if i == nil {
		return 0, nil
	}
	switch t := i.(type) {
	case []byte:
		u64, err := strconv.ParseUint(string(t), 10, 64)
		if err != nil {
			logrus.WithError(err).Debugf("could not convert %#v to uint64", t)
		}
		return u64, err
	case string:
		u64, err := strconv.ParseUint(t, 10, 64)
		if err != nil {
			logrus.WithError(err).Debugf("could not convert %#v to uint64", t)
		}
		return u64, err
	case int:
		return uint64(t), nil
	case int64:
		return uint64(t), nil
	case uint:
		return uint64(t), nil
	case uint64:
		return t, nil
	case float64:
		return uint64(t), nil
	case json.Number:
		u64, err := strconv.ParseUint(t.String(), 10, 64)
		if err != nil {
			logrus.WithError(err).Debugf("could not convert %#v to uint64", t)
		}
		return u64, err
	case nil:
		return 0, nil
	default:
		err := fmt.Errorf("could not convert %#v to uint64", i)
		logrus.Debug(err.Error())
		return 0, err
	}
}

//InterfaceToBool makes a bool from interface.
func InterfaceToBool(i interface{}) bool {
	ret, _ := InterfaceToBoolE(i) // nolint:errcheck
	return ret
}

//InterfaceToBoolE makes a bool from interface with error reporting.
func InterfaceToBoolE(i interface{}) (bool, error) {
	switch t := i.(type) {
	case []byte:
		b, err := strconv.ParseBool(string(t))
		if err != nil {
			logrus.WithError(err).Debugf("could not convert %#v to bool", t)
		}
		return b, err
	case string:
		b, err := strconv.ParseBool(t)
		if err != nil {
			logrus.WithError(err).Debugf("could not convert %#v to bool", t)
		}
		return b, err
	case bool:
		return t, nil
	case int64:
		return t == 1, nil
	case float64:
		return t == 1, nil
	case nil:
		return false, nil
	default:
		err := fmt.Errorf("could not convert %#v to bool", i)
		logrus.Debug(err.Error())
		return false, err
	}
}

//InterfaceToString makes a string from interface.
func InterfaceToString(i interface{}) string {
	ret, _ := InterfaceToStringE(i) // nolint:errcheck
	return ret
}

//InterfaceToStringE makes a string from interface with error reporting.
func InterfaceToStringE(i interface{}) (string, error) {
	switch t := i.(type) {
	case []byte:
		return string(t), nil
	case string:
		return t, nil
	case nil:
		return "", nil
	default:
		err := fmt.Errorf("could not convert %#v to string", i)
		logrus.Debug(err.Error())
		return "", err
	}
}

//InterfaceToStringList makes a string list from interface.
func InterfaceToStringList(i interface{}) []string {
	ret, _ := InterfaceToStringListE(i) // nolint:errcheck
	return ret
}

//InterfaceToStringListE makes a string list from interface with error reporting.
func InterfaceToStringListE(i interface{}) ([]string, error) {
	switch t := i.(type) {
	case []string:
		return t, nil
	case []interface{}:
		result := []string{}
		lastErr := error(nil)
		for _, s := range t {
			str, err := InterfaceToStringE(s)
			if err != nil {
				logrus.WithError(err).Debugf("could not convert %#v to []string", i)
				lastErr = err
			}
			result = append(result, str)
		}
		return result, lastErr
	case nil:
		return nil, nil
	default:
		err := fmt.Errorf("could not convert %#v to []string", i)
		logrus.Debug(err.Error())
		return nil, err
	}
}

//InterfaceToInt64List makes a int64 list from interface.
func InterfaceToInt64List(i interface{}) []int64 {
	ret, _ := InterfaceToInt64ListE(i) // nolint:errcheck
	return ret
}

//InterfaceToInt64ListE makes a int64 list from interface with error reporting.
func InterfaceToInt64ListE(i interface{}) ([]int64, error) {
	switch t := i.(type) {
	case []int64:
		return t, nil
	case []interface{}:
		result := []int64{}
		lastErr := error(nil)
		for _, s := range t {
			i64, err := InterfaceToInt64E(s)
			if err != nil {
				logrus.WithError(err).Debugf("could not convert %#v to []int64", i)
				lastErr = err
			}
			result = append(result, i64)
		}
		return result, lastErr
	case nil:
		return nil, nil
	default:
		err := fmt.Errorf("could not convert %#v to []int64", i)
		logrus.Debug(err.Error())
		return nil, err
	}
}

//InterfaceToInterfaceList makes a interface list from interface.
func InterfaceToInterfaceList(i interface{}) []interface{} {
	ret, _ := InterfaceToInterfaceListE(i) // nolint:errcheck
	return ret
}

//InterfaceToInterfaceListE makes a interface list from interface with error reporting.
func InterfaceToInterfaceListE(i interface{}) ([]interface{}, error) {
	t, ok := i.([]interface{})
	if !ok {
		err := fmt.Errorf("could not convert %#v to []interface{}", i)
		return t, err
	}
	return t, nil
}

//InterfaceToStringMap makes a string map.
func InterfaceToStringMap(i interface{}) map[string]string {
	ret, _ := InterfaceToStringMapE(i) // nolint:errcheck
	return ret
}

//InterfaceToStringMapE makes a string map with error reporting.
func InterfaceToStringMapE(i interface{}) (map[string]string, error) {
	t, ok := i.(map[string]string)
	if !ok {
		err := fmt.Errorf("could not convert %#v to map[string]string", i)
		return t, err
	}
	return t, nil
}

//InterfaceToInt64Map makes a string map.
func InterfaceToInt64Map(i interface{}) map[string]int64 {
	ret, _ := InterfaceToInt64MapE(i) // nolint:errcheck
	return ret
}

//InterfaceToInt64MapE makes a string map with error reporting.
func InterfaceToInt64MapE(i interface{}) (map[string]int64, error) {
	t, ok := i.(map[string]int64)
	if !ok {
		err := fmt.Errorf("could not convert %#v to map[string]int64", i)
		return t, err
	}
	return t, nil
}

//InterfaceToInterfaceMap makes a interface map.
func InterfaceToInterfaceMap(i interface{}) map[string]interface{} {
	ret, _ := InterfaceToInterfaceMapE(i) // nolint:errcheck
	return ret
}

//InterfaceToInterfaceMapE makes a interface map with error reporting.
func InterfaceToInterfaceMapE(i interface{}) (map[string]interface{}, error) {
	t, ok := i.(map[string]interface{})
	if !ok {
		err := fmt.Errorf("could not convert %#v to map[string]interface{}", i)
		return t, err
	}
	return t, nil
}

//InterfaceToFloat makes a float.
func InterfaceToFloat(i interface{}) float64 {
	ret, _ := InterfaceToFloatE(i) // nolint:errcheck
	return ret
}

//InterfaceToFloatE makes a float with error reporting.
// nolint: gocyclo
func InterfaceToFloatE(i interface{}) (float64, error) {
	f64, ok := i.(float64) //nolint: errcheck
	if ok {
		return f64, nil
	}
	switch t := i.(type) {
	case []byte:
		f, err := strconv.ParseFloat(string(t), 64)
		if err != nil {
			logrus.WithError(err).Debugf("could not convert %#v to float", t)
		}
		return f, err
	case string:
		f, err := strconv.ParseFloat(t, 64)
		if err != nil {
			logrus.WithError(err).Debugf("could not convert %#v to float", t)
		}
		return f, err
	case int:
		return float64(t), nil
	case int64:
		return float64(t), nil
	case nil:
		return 0, nil
	case json.Number:
		f, err := t.Float64()
		if err != nil {
			logrus.WithError(err).Debugf("could not convert %#v to float64", t)
		}
		return f, err
	default:
		err := fmt.Errorf("could not convert %#v to float64", i)
		logrus.Debug(err.Error())
		return f64, err
	}
}

//InterfaceToBytes makes a bytes from interface.
func InterfaceToBytes(i interface{}) []byte {
	ret, _ := InterfaceToBytesE(i) // nolint:errcheck
	return ret
}

//InterfaceToBytesE makes a bytes from interface with error reporting.
func InterfaceToBytesE(i interface{}) ([]byte, error) {
	switch t := i.(type) { //nolint: errcheck
	case []byte:
		return t, nil
	case string:
		return []byte(t), nil
	default:
		err := fmt.Errorf("could not convert %#v to []byte", i)
		logrus.Debug(err.Error())
	}
	return []byte{}, nil
}

//GetUUIDFromInterface get a UUID from an interface with error reporting.
func GetUUIDFromInterface(rawProperties interface{}) string {
	ret, _ := GetUUIDFromInterfaceE(rawProperties) // nolint:errcheck
	return ret
}

//GetUUIDFromInterfaceE get a UUID from an interface with error reporting.
func GetUUIDFromInterfaceE(rawProperties interface{}) (string, error) {
	properties, ok := rawProperties.(map[string]interface{})
	if !ok {
		return "", fmt.Errorf("invalid data format: no properties mapping")
	}

	rawUUID, ok := properties["uuid"]
	if !ok {
		return "", errors.New("data does not contain required UUID property")
	}

	uuid, ok := rawUUID.(string)
	if !ok {
		return "", fmt.Errorf("value UUID should be string instead of %T", uuid)
	}
	return uuid, nil
}
