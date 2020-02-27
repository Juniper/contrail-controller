package basemodels_test

import (
	"testing"

	"github.com/gogo/protobuf/types"
	"github.com/stretchr/testify/assert"

	. "github.com/Juniper/asf/pkg/models/basemodels"
)

func TestMapToFieldMask(t *testing.T) {
	tests := []struct {
		name     string
		request  map[string]interface{}
		expected types.FieldMask
	}{{
		name:     "returns nil paths given no data",
		expected: types.FieldMask{Paths: nil},
	}, {
		name: "returns correct paths given data with maps",
		request: map[string]interface{}{
			"simple": 1,
			"nested": map[string]interface{}{"inner": 1},
		},
		expected: types.FieldMask{Paths: []string{"simple", "nested.inner"}},
	}, {
		name: "returns correct paths given data with types implementing toMapper()",
		request: map[string]interface{}{
			"object": mockToMapper{m: map[string]interface{}{
				"simple": 1,
				"nested": map[string]interface{}{"inner": 1},
			}},
		},
		expected: types.FieldMask{Paths: []string{"object.simple", "object.nested.inner"}},
	}}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			fm := MapToFieldMask(tt.request)

			assert.Len(t, fm.Paths, len(tt.expected.Paths))
			for _, p := range fm.Paths {
				assert.Contains(t, tt.expected.Paths, p)
			}
		})
	}
}

type mockToMapper struct {
	m map[string]interface{}
}

func (m mockToMapper) ToMap() map[string]interface{} {
	return m.m
}

func TestGetFromMapByPath(t *testing.T) {
	tests := []struct {
		name      string
		data      map[string]interface{}
		path      []string
		wantValue interface{}
		wantOk    bool
	}{
		{name: "nil"},
		{name: "nil map", path: []string{"asd", "bar"}},
		{name: "empty map", data: map[string]interface{}{}, path: []string{"asd"}},
		{name: "empty map nested path", data: map[string]interface{}{}, path: []string{"asd", "bar"}},
		{name: "first level exists", data: map[string]interface{}{"asd": 1}, path: []string{"asd", "bar"}},
		{name: "flat", data: map[string]interface{}{"asd": 1}, path: []string{"asd"}, wantValue: 1, wantOk: true},
		{
			name:      "nested",
			data:      map[string]interface{}{"asd": map[string]interface{}{"bar": "value"}},
			path:      []string{"asd", "bar"},
			wantValue: "value",
			wantOk:    true,
		},
		{name: "key exists value is nil", data: map[string]interface{}{"asd": nil}, path: []string{"asd"}, wantOk: true},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			gotValue, gotOk := GetFromMapByPath(tt.data, tt.path)
			assert.Equal(t, tt.wantOk, gotOk)
			assert.Equal(t, tt.wantValue, gotValue)
		})
	}
}

func TestApplyFieldMask(t *testing.T) {
	tests := []struct {
		name string
		m    map[string]interface{}
		fm   types.FieldMask
		want map[string]interface{}
	}{
		{name: "nil"},
		{name: "empty map empty fieldmask", m: map[string]interface{}{}, want: map[string]interface{}{}},
		{
			name: "empty map",
			m:    map[string]interface{}{},
			fm:   types.FieldMask{Paths: []string{"asd", "foo"}},
			want: map[string]interface{}{},
		},
		{name: "nonempty map empty fieldmask", m: map[string]interface{}{"key": "value"}, want: map[string]interface{}{}},
		{
			name: "fieldmask matching",
			m:    map[string]interface{}{"key": "value"},
			fm:   types.FieldMask{Paths: []string{"key"}},
			want: map[string]interface{}{"key": "value"},
		},
		{
			name: "mixed keys",
			m:    map[string]interface{}{"key": "value", "map": map[string]interface{}{"inner": 123}},
			fm:   types.FieldMask{Paths: []string{"key", "map.inner"}},
			want: map[string]interface{}{"key": "value", "map": map[string]interface{}{"inner": 123}},
		},
		{
			name: "three level nest",
			m:    map[string]interface{}{"map": map[string]interface{}{"inner": map[string]interface{}{"deep": true}}},
			fm:   types.FieldMask{Paths: []string{"key", "map.inner", "map.inner.deep"}},
			want: map[string]interface{}{"map": map[string]interface{}{"inner": map[string]interface{}{"deep": true}}},
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := ApplyFieldMask(tt.m, tt.fm)
			assert.Equal(t, tt.want, got)
		})
	}
}

func TestFieldMaskContains(t *testing.T) {
	tests := []struct {
		name             string
		requestedFM      *types.FieldMask
		requestedFields  []string
		expectedResponse bool
	}{
		{
			name:             "field mask contains requested field",
			requestedFM:      &types.FieldMask{Paths: []string{"first", "second"}},
			requestedFields:  []string{"first"},
			expectedResponse: true,
		},
		{
			name:             "field mask shouldn't contain path if only prefix matches",
			requestedFM:      &types.FieldMask{Paths: []string{"first", "second"}},
			requestedFields:  []string{"fir"},
			expectedResponse: false,
		},
		{
			name:             "field mask contains requested prefix field",
			requestedFM:      &types.FieldMask{Paths: []string{"test.first.first", "test.second.first"}},
			requestedFields:  []string{"test"},
			expectedResponse: true,
		},
		{
			name:             "field mask contains requested prefix compelx field",
			requestedFM:      &types.FieldMask{Paths: []string{"test.first.first", "test.second.first"}},
			requestedFields:  []string{"test", "second"},
			expectedResponse: true,
		},
		{
			name:             "field mask contains requested complex field",
			requestedFM:      &types.FieldMask{Paths: []string{"test.first.first", "test.second.first"}},
			requestedFields:  []string{"test", "first", "first"},
			expectedResponse: true,
		},
		{
			name:             "field mask doesn't contain requested complex field",
			requestedFM:      &types.FieldMask{Paths: []string{"test.first.first", "test.second.first"}},
			requestedFields:  []string{"test", "third", "first"},
			expectedResponse: false,
		},
		{
			name:             "field mask doesn't contain requested field",
			requestedFM:      &types.FieldMask{Paths: []string{"first", "second"}},
			requestedFields:  []string{"third"},
			expectedResponse: false,
		},
		{
			name:             "field mask is empty",
			requestedFM:      &types.FieldMask{Paths: []string{}},
			requestedFields:  []string{"first"},
			expectedResponse: false,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			contains := FieldMaskContains(tt.requestedFM, tt.requestedFields...)
			assert.Equal(t, tt.expectedResponse, contains)
		})
	}
}

func TestJoinPath(t *testing.T) {
	tests := []struct {
		name             string
		requestedFields  []string
		expectedResponse string
	}{
		{
			name:             "concatenate multiple fields",
			requestedFields:  []string{"first", "second", "third"},
			expectedResponse: "first.second.third",
		},
		{
			name:             "join only one field",
			requestedFields:  []string{"first"},
			expectedResponse: "first",
		},
		{
			name:             "no fields provided",
			requestedFields:  []string{},
			expectedResponse: "",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			path := JoinPath(tt.requestedFields...)
			assert.Equal(t, tt.expectedResponse, path)
		})
	}
}

func TestFieldMaskRemove(t *testing.T) {
	tests := []struct {
		name     string
		fm       *types.FieldMask
		fields   []string
		expected []string
	}{
		{name: "empty"},
		{
			name:     "does not contain desired string",
			fm:       &types.FieldMask{Paths: []string{"asd"}},
			fields:   nil,
			expected: []string{"asd"},
		},
		{
			name:     "contains desired string",
			fm:       &types.FieldMask{Paths: []string{"field", "asd", "path"}},
			fields:   []string{"asd"},
			expected: []string{"field", "path"},
		},
		{
			name:     "multiple values",
			fm:       &types.FieldMask{Paths: []string{"field", "asd", "path"}},
			fields:   []string{"asd", "field"},
			expected: []string{"path"},
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			FieldMaskRemove(tt.fm, tt.fields...)
			assert.Equal(t, tt.expected, tt.fm.GetPaths())
		})
	}
}
