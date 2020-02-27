package format

import (
	"testing"

	"github.com/gogo/protobuf/types"
	"github.com/stretchr/testify/assert"
)

func TestCaseConversions(t *testing.T) {
	tests := []struct {
		goType   string
		schemaID string
	}{
		{goType: "APIList", schemaID: "api_list"},
		{goType: "L4Policy", schemaID: "l4_policy"},
		{goType: "E2Service", schemaID: "e2_service"},
		{goType: "AppleBanana", schemaID: "apple_banana"},
		{goType: "AwsNode", schemaID: "aws_node"},
		{goType: "KubernetesMasterNode", schemaID: "kubernetes_master_node"},
	}

	for _, tt := range tests {
		t.Run(tt.goType, func(t *testing.T) {
			t.Run("CamelToSnakeCase", func(t *testing.T) {
				assert.Equal(t, tt.schemaID, CamelToSnake(tt.goType))
			})
			t.Run("SnakeToCamel", func(t *testing.T) {
				assert.Equal(t, tt.goType, SnakeToCamel(tt.schemaID))
			})
		})
	}
}

func TestCheckPath(t *testing.T) {
	testData := []struct {
		path      []string
		fieldMask *types.FieldMask
		fails     bool
	}{
		{
			path:      []string{"first", "test"},
			fieldMask: &types.FieldMask{Paths: []string{"first.test", "second.test", "third"}},
			fails:     false,
		},
		{
			path:      []string{"second", "test", "case"},
			fieldMask: &types.FieldMask{Paths: []string{"first.test", "second.test.case", "third"}},
			fails:     false,
		},
		{
			path:      []string{"third"},
			fieldMask: &types.FieldMask{Paths: []string{"first.test", "second.test", "third"}},
			fails:     false,
		},
		{
			path:      []string{"bad", "test"},
			fieldMask: &types.FieldMask{Paths: []string{"first.test", "second.test", "third"}},
			fails:     true,
		},
		{
			path:      []string{"it", "should", "fail"},
			fieldMask: &types.FieldMask{Paths: []string{"first.test", "second.test", "third"}},
			fails:     true,
		},
		{
			path:      []string{"fail"},
			fieldMask: &types.FieldMask{Paths: []string{"first.test", "second.test", "third"}},
			fails:     true,
		},
	}

	for _, tt := range testData {
		isInFieldMask := CheckPath(tt.fieldMask, tt.path)
		if tt.fails {
			assert.False(t, isInFieldMask)
		} else {
			assert.True(t, isInFieldMask)
		}
	}

}

func TestRemoveFromStringSlice(t *testing.T) {
	tests := []struct {
		name     string
		slice    []string
		values   map[string]struct{}
		expected []string
	}{
		{
			name:     "does nothing when no values given",
			slice:    []string{"foo", "bar", "baz", "hoge"},
			values:   nil,
			expected: []string{"foo", "bar", "baz", "hoge"},
		},
		{
			name:  "removes single value from slice",
			slice: []string{"foo", "bar", "baz", "hoge"},
			values: map[string]struct{}{
				"bar": {},
			},
			expected: []string{"foo", "baz", "hoge"},
		},
		{
			name:  "removes multiple values from slice",
			slice: []string{"foo", "bar", "baz", "hoge"},
			values: map[string]struct{}{
				"bar": {},
				"baz": {},
			},
			expected: []string{"foo", "hoge"},
		},
		{
			name:  "removes multiple values from slice including first",
			slice: []string{"foo", "bar", "baz", "hoge"},
			values: map[string]struct{}{
				"foo": {},
				"bar": {},
				"baz": {},
			},
			expected: []string{"hoge"},
		},
		{
			name:  "removes multiple values from slice including last",
			slice: []string{"foo", "bar", "baz", "hoge"},
			values: map[string]struct{}{
				"bar":  {},
				"baz":  {},
				"hoge": {},
			},
			expected: []string{"foo"},
		},
		{
			name:  "removes all values from slice given all values",
			slice: []string{"foo", "bar", "baz", "hoge"},
			values: map[string]struct{}{
				"foo":  {},
				"bar":  {},
				"baz":  {},
				"hoge": {},
			},
			expected: []string(nil),
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			s := RemoveFromStringSlice(tt.slice, tt.values)

			assert.Equal(t, tt.expected, s)
		})
	}
}
