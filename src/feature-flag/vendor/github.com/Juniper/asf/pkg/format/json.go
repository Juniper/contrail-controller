package format

import (
	"encoding/json"
	"strings"

	"github.com/sirupsen/logrus"
)

//MustJSON Marshal json
func MustJSON(data interface{}) string {
	b, err := json.Marshal(data)
	if err != nil {
		logrus.WithError(err).Debug("failed to marshal")
		return ""
	}
	return string(b)
}

// access method iterates the dataSource map and returns the sub map for the given path.
// in case of isSet set to true, sub map will be created for the path.
func access(dataSource map[string]interface{}, path []string, ok *bool, isSet bool) map[string]interface{} {
	var currentSource map[string]interface{}
	if len(path) == 0 {
		*ok = true
		return dataSource
	}
	currentAttr := CamelToSnake(path[0])
	path = path[1:]
	if mapValue, found := dataSource[currentAttr]; found {
		switch mapValue.(type) {
		case bool, string, float64, []interface{}:
			if len(path) != 0 {
				*ok = false
			} else {
				currentSource = dataSource
			}
		case map[string]interface{}:
			currentSource = access(mapValue.(map[string]interface{}), path, ok, isSet)
		default:
			*ok = false
		}
	} else if isSet {
		dataSource[currentAttr] = map[string]interface{}{}
		currentSource = dataSource[currentAttr].(map[string]interface{}) //nolint: errcheck
		for _, element := range path {
			currentSource[element] = map[string]interface{}{}
			currentSource = currentSource[element].(map[string]interface{}) //nolint: errcheck
		}
		*ok = true
	} else {
		*ok = false
	}
	return currentSource
}

//GetValueByPath method returns the value for the given path form the nested map.
//The path currently does not support array index.
func GetValueByPath(dataSource map[string]interface{}, path string, delimiter string) (value interface{}, ok bool) {
	pathAsList, attributeName := getPathAsList(path, delimiter)
	dataSourceRef := access(dataSource, pathAsList, &ok, false)
	if ok {
		value, ok = dataSourceRef[attributeName]
	}
	return value, ok
}

//SetValueByPath method set the value for the given path in the nested map.
//The path currently does not support slice item assignment.
func SetValueByPath(dataSource map[string]interface{}, path string, delimiter string, value interface{}) (ok bool) {
	pathAsList, attributeName := getPathAsList(path, delimiter)
	dataSourceRef := access(dataSource, pathAsList, &ok, true)
	if ok {
		dataSourceRef[attributeName] = value
	}
	return ok
}

func getPathAsList(path string, delimiter string) ([]string, string) {
	pathAsList := strings.Split(path, delimiter)
	if pathAsList[0] == "" {
		pathAsList = pathAsList[1:] //ignore the leading empty string on split of .a.b.c
	}
	attributeName := CamelToSnake(pathAsList[len(pathAsList)-1])
	pathAsList = pathAsList[:len(pathAsList)-1]
	return pathAsList, attributeName
}
