package basemodels

import (
	"fmt"
	"strings"

	"github.com/gogo/protobuf/types"

	"github.com/Juniper/asf/pkg/format"
)

// MapToFieldMask returns updated fields masks.
func MapToFieldMask(data map[string]interface{}) types.FieldMask {
	var mask types.FieldMask
	mask.Paths = paths(data, "")
	return mask
}

type toMapper interface {
	ToMap() map[string]interface{}
}

func paths(data map[string]interface{}, prefix string) []string {
	var result []string
	for key, value := range data {
		switch v := value.(type) {
		case map[string]interface{}:
			if len(v) == 0 {
				result = append(result, prefix+key)
				continue
			}
			if prefix != "" {
				result = append(result, paths(v, prefix+key+".")...)
			} else {
				result = append(result, paths(v, key+".")...)
			}
		case toMapper:
			m := v.ToMap()
			if len(m) == 0 {
				result = append(result, prefix+key)
				continue
			}
			if prefix != "" {
				result = append(result, paths(m, prefix+key+".")...)
			} else {
				result = append(result, paths(m, key+".")...)
			}
		default:
			result = append(result, prefix+key)
		}
	}
	return result
}

// GetFromMapByPath gets map value by provided as slice of strings.
func GetFromMapByPath(data map[string]interface{}, path []string) (value interface{}, ok bool) {
	if len(path) == 0 {
		return nil, false
	}
	last := len(path) - 1
	for _, field := range path[:last] {
		next, _ := data[field].(map[string]interface{}) //nolint: errcheck
		if next == nil {
			return nil, false
		}
		data = next
	}
	value, ok = data[path[last]]
	return value, ok
}

// ApplyFieldMask creates new map that contain only fields specified in fieldmask.
func ApplyFieldMask(m map[string]interface{}, fm types.FieldMask) map[string]interface{} {
	if m == nil {
		return nil
	}
	result := map[string]interface{}{}
	for _, path := range fm.Paths {
		fields := strings.Split(path, ".")
		value, ok := GetFromMapByPath(m, fields)
		if len(fields) == 0 || !ok {
			continue
		}
		cur := result
		for _, field := range fields[:len(fields)-1] {
			nested := nestMap(cur, field)
			cur = nested
		}
		cur[fields[len(fields)-1]] = value
	}
	return result
}

func nestMap(m map[string]interface{}, key string) map[string]interface{} {
	nested, ok := m[key].(map[string]interface{}) //nolint: errcheck
	if ok {
		return nested
	}
	nested = map[string]interface{}{}
	m[key] = nested
	return nested
}

// JoinPath concatenates arguments and returns path.
func JoinPath(fields ...string) string {
	var path string
	for i, f := range fields {
		if i == 0 {
			path = f
			continue
		}

		path = fmt.Sprintf(path + "." + f)
	}

	return path
}

// FieldMaskContains checks if given field mask contains requested string.
func FieldMaskContains(fm *types.FieldMask, fields ...string) bool {

	path := JoinPath(fields...)
	for _, p := range fm.GetPaths() {
		if strings.HasPrefix(p, path) {
			suffix := strings.TrimPrefix(p, path)
			if len(suffix) == 0 || strings.HasPrefix(suffix, ".") {
				return true
			}
		}
	}

	return false
}

// FieldMaskAppend appends to field mask if it doesn't contain requested string.
func FieldMaskAppend(fm *types.FieldMask, fields ...string) {
	if fm == nil || FieldMaskContains(fm, fields...) {
		return
	}
	path := JoinPath(fields...)
	fm.Paths = append(fm.Paths, path)
}

// FieldMaskRemove removes given values from field mask.
func FieldMaskRemove(fm *types.FieldMask, fields ...string) {
	if fm == nil {
		return
	}
	paths := make([]string, 0, len(fm.Paths))
	for _, val := range fm.Paths {
		if format.ContainsString(fields, val) {
			continue
		}
		paths = append(paths, val)
	}
	fm.Paths = paths
}
