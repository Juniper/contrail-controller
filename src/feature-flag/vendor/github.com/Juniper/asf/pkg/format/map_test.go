package format

import (
	"encoding/json"
	"reflect"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

type Map map[string]string

func (f *Map) ApplyMap(m map[string]interface{}) error {
	return nil
}

type SomeStruct struct {
	Integer                  int  `json:"int_tag"`
	Boolean                  bool `json:"boolean_tag"`
	String                   string
	ArrayOfStrings           []string         `json:"str_array"`
	ArrayOfStructs           []AnotherStruct  `json:"struct_array"`
	ArrayOfPointersToStructs []*AnotherStruct `json:"pointer_to_struct_array"`
	Struct                   AnotherStruct
	PointerToStruct          *AnotherStruct
	Interface                interface{}
	Map                      Map
}

type StructWithPointerToNonStruct struct {
	PointerToString *string
}

type AnotherStruct struct {
	A string
}

func TestApplyMapWithJSONUnmarshalMaps(t *testing.T) {
	var someString = "hoge"
	tests := []struct {
		name     string
		expected interface{}
		fails    bool
	}{
		{
			name: "nil object",
		},
		{
			name: "pointer to non struct field",
			expected: &StructWithPointerToNonStruct{
				PointerToString: &someString,
			},
			fails: true,
		},
		{
			name: "integer field",
			expected: &SomeStruct{
				Integer: 234,
			},
		},
		{
			name: "array field",
			expected: &SomeStruct{
				ArrayOfStrings: []string{"aa", "bb"},
			},
		},
		{
			name: "struct field",
			expected: &SomeStruct{
				Struct: AnotherStruct{
					A: "hoge",
				},
			},
		},
		{
			name: "struct array field",
			expected: &SomeStruct{
				ArrayOfStructs: []AnotherStruct{
					{
						A: "aa",
					},
					{
						A: "bb",
					},
				},
			},
		},
		{
			name: "pointer to struct array field",
			expected: &SomeStruct{
				ArrayOfPointersToStructs: []*AnotherStruct{
					{
						A: "aa",
					},
					{
						A: "bb",
					},
				},
			},
		},
		{
			name: "pointer to struct field",
			expected: &SomeStruct{
				PointerToStruct: &AnotherStruct{
					A: "hoge",
				},
			},
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			raw, err := json.Marshal(tt.expected)
			require.NoError(t, err)
			var m map[string]interface{}
			require.NoError(t, json.Unmarshal(raw, &m))
			object := zeroObject(tt.expected)

			err = ApplyMap(m, object)

			if tt.fails {
				assert.Error(t, err)
			} else {
				assert.NoError(t, err)
				assert.Equal(t, tt.expected, object)
			}
		})
	}
}

func zeroObject(i interface{}) interface{} {
	if t := reflect.TypeOf(i); t != nil {
		return reflect.New(indirect(t)).Interface()
	}
	return nil
}

func TestApplyMap(t *testing.T) {
	var someInt = 6
	var nilStruct *SomeStruct

	type args struct {
		m map[string]interface{}
		o interface{}
	}
	tests := []struct {
		name     string
		args     args
		expected interface{}
		fails    bool
	}{
		{
			name: "apply nil map to nil",
		},
		{
			name: "apply_non nil map to nil",
			args: args{
				m: map[string]interface{}{
					"hoge": "hoge",
				},
			},
			fails: true,
		},
		{
			name: "apply integer to array",
			args: args{
				m: map[string]interface{}{
					"str_array": 1,
				},
			},
			fails: true,
		},
		{
			name: "pointer to non struct",
			args: args{
				o: &someInt,
				m: map[string]interface{}{
					"hoge": "hoge",
				},
			},
			expected: &someInt,
			fails:    true,
		},
		{
			name: "non struct",
			args: args{
				o: someInt,
				m: map[string]interface{}{
					"hoge": "hoge",
				},
			},
			expected: someInt,
			fails:    true,
		},
		{
			name: "struct",
			args: args{
				o: &SomeStruct{},
				m: map[string]interface{}{
					"hoge": "hoge",
				},
			},
			expected: &SomeStruct{},
		},
		{
			name: "pointer to nil struct",
			args: args{
				o: &nilStruct,
				m: map[string]interface{}{
					"hoge": "hoge",
				},
			},
			expected: &nilStruct,
		},
		{
			name: "apply fields",
			args: args{
				o: &SomeStruct{},
				m: map[string]interface{}{
					"int_tag":     1,
					"boolean_tag": true,
					"String":      "hoge",
				},
			},
			expected: &SomeStruct{
				Integer: 1,
				Boolean: true,
				String:  "hoge",
			},
		},
		{
			name: "pointer_to struct under interface field",
			args: args{
				o: &SomeStruct{
					Interface: &AnotherStruct{},
				},
				m: map[string]interface{}{
					"Interface": map[string]interface{}{
						"A": "aaa",
					},
				},
			},
			expected: &SomeStruct{
				Interface: &AnotherStruct{A: "aaa"},
			},
		},
		{
			name: "struct under interface field",
			args: args{
				o: &SomeStruct{
					Interface: AnotherStruct{},
				},
				m: map[string]interface{}{
					"Interface": map[string]interface{}{
						"A": "aaa",
					},
				},
			},
			expected: &SomeStruct{
				Interface: AnotherStruct{},
			},
			fails: true,
		},
		{
			name: "apply_invalid value to integer field",
			args: args{
				o: &SomeStruct{},
				m: map[string]interface{}{
					"int_tag": "hoge",
				},
			},
			expected: &SomeStruct{},
			fails:    true,
		},
		{
			name: "apply_invalid value to string field",
			args: args{
				o: &SomeStruct{},
				m: map[string]interface{}{
					"String": 123,
				},
			},
			expected: &SomeStruct{},
			fails:    true,
		},
		{
			name: "apply []A to []A",
			args: args{
				o: &SomeStruct{},
				m: map[string]interface{}{
					"struct_array": []AnotherStruct{
						{
							A: "hoge",
						},
					},
				},
			},
			expected: &SomeStruct{
				ArrayOfStructs: []AnotherStruct{
					{
						A: "hoge",
					},
				},
			},
		},
		{
			name: "try to apply []A to []B",
			args: args{
				o: &SomeStruct{},
				m: map[string]interface{}{
					"struct_array": []SomeStruct{
						{},
					},
				},
			},
			expected: &SomeStruct{},
			fails:    true,
		},
		{
			name: "apply []*A to []*A",
			args: args{
				o: &SomeStruct{},
				m: map[string]interface{}{
					"pointer_to_struct_array": []*AnotherStruct{
						{
							A: "hoge",
						},
					},
				},
			},
			expected: &SomeStruct{
				ArrayOfPointersToStructs: []*AnotherStruct{
					{
						A: "hoge",
					},
				},
			},
		},
		{
			name: "try to apply []*A to []*B",
			args: args{
				o: &SomeStruct{},
				m: map[string]interface{}{
					"pointer_to_struct_array": []*SomeStruct{
						{},
					},
				},
			},
			expected: &SomeStruct{},
			fails:    true,
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			err := ApplyMap(tt.args.m, tt.args.o)

			if tt.fails {
				assert.Error(t, err)
			} else {
				assert.NoError(t, err)
			}
			assert.Equal(t, tt.expected, tt.args.o)
		})
	}
}
