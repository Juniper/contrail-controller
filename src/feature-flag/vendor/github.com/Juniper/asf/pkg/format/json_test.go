package format

import (
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestMustJSONReturnsEmptyStringOnMarshalError(t *testing.T) {
	invalidData := map[float64]interface{}{1.337: "value"}
	assert.Equal(t, "", MustJSON(invalidData))
}

func TestGetValueByPathSingleField(t *testing.T) {
	current := map[string]interface{}{"name": "Tom"}
	result, ok := GetValueByPath(current, "name", ".")
	assert.Equal(t, true, ok)
	assert.Equal(t, "Tom", result)
}

func TestGetValueByPathSingleDeep(t *testing.T) {
	current := map[string]interface{}{"name": map[string]interface{}{"first": "Tom", "last": "Hanks"}}
	result, ok := GetValueByPath(current, ".name.first", ".")
	assert.Equal(t, true, ok)
	assert.Equal(t, "Tom", result)
	result, ok = GetValueByPath(current, "name.last", ".")
	assert.Equal(t, true, ok)
	assert.Equal(t, "Hanks", result)

}

func TestGetValueByPathDeepDeep(t *testing.T) {
	current := map[string]interface{}{"one": map[string]interface{}{"two": map[string]interface{}{
		"three": map[string]interface{}{"four": 4}}}}
	result, ok := GetValueByPath(current, "one.two.three.four", ".")
	assert.Equal(t, true, ok)
	assert.Equal(t, 4, result)
}

func TestGetValueByPathFieldNotExisting(t *testing.T) {
	current := map[string]interface{}{"name": map[string]interface{}{"first": "Tom"}}
	result, ok := GetValueByPath(current, "name.last", ".")
	assert.Equal(t, false, ok)
	assert.Equal(t, nil, result)
}

func TestSetValueByPathFieldExisting(t *testing.T) {
	current := map[string]interface{}{"name": "Tom"}
	ok := SetValueByPath(current, "name", ".", "Jerry")
	assert.Equal(t, true, ok)
	assert.Equal(t, "Jerry", current["name"])
}

func TestSetValueByPathFieldNotExisting(t *testing.T) {
	current := map[string]interface{}{"name": map[string]interface{}{"first": "Tom"}}
	ok := SetValueByPath(current, "name.last", ".", "Hanks")
	assert.Equal(t, true, ok)
	assert.Equal(t, "Hanks", current["name"].(map[string]interface{})["last"])
}

func TestSetValueByPathDeep(t *testing.T) {
	current := map[string]interface{}{"name": map[string]interface{}{"first": "Tom", "last": "Hardy"}}
	ok := SetValueByPath(current, "name.last", ".", "Hanks")
	assert.Equal(t, true, ok)
	assert.Equal(t, "Hanks", current["name"].(map[string]interface{})["last"])
}

func TestSetValueByPathDeepDeep(t *testing.T) {
	current := map[string]interface{}{"one": map[string]interface{}{}}
	ok := SetValueByPath(current, "one.two.three.four", ".", 4)
	assert.Equal(t, true, ok)
	assert.Equal(t, 4, current["one"].(map[string]interface{})["two"].(map[string]interface {
	})["three"].(map[string]interface{})["four"])
}
