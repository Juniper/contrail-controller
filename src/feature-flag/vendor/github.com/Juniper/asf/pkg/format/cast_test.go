package format

import (
	"testing"

	"github.com/stretchr/testify/assert"
)

var numericTests = []struct {
	name     string
	input    interface{}
	expected int
}{
	{
		name:     "casting int(4) to int",
		input:    int(4),
		expected: 4,
	},
	{
		name:     "casting int64(4) to int",
		input:    int64(4),
		expected: 4,
	},
	{
		name:     "casting float64(4.02) to int",
		input:    float64(4.02),
		expected: 4,
	},
	{
		name:     "casting int8(4) to int is not supported",
		input:    int8(4),
		expected: 0,
	},
	{
		name:     `casting []byte("4") to int`,
		input:    []byte("4"),
		expected: 4,
	},
	{
		name:     `casting negative []byte("-4") to int`,
		input:    []byte("-4"),
		expected: -4,
	},
	{
		name:     `casting byte slice with wrong string escape []byte(\""-4"\")`,
		input:    []byte(`\"-4\"`),
		expected: 0,
	},
	{
		name:     `casting big negative num []byte("-9223372036854775808") to int`,
		input:    []byte("-9223372036854775808"),
		expected: -9223372036854775808,
	},
	{
		name:     `casting "4" to int`,
		input:    "4",
		expected: 4,
	},
	{
		name:     `casting "-4" to int4`,
		input:    "-4",
		expected: -4,
	},
	{
		name:     `casting "-9223372036854775808" to int`,
		input:    "-9223372036854775808",
		expected: -9223372036854775808,
	},
}

func TestInterfaceToInt(t *testing.T) {
	i, err := InterfaceToIntE(nil)
	assert.Nil(t, err)
	assert.Equal(t, 0, i)
	for _, tt := range numericTests {
		t.Run(tt.name, func(t *testing.T) {
			i, err = InterfaceToIntE(tt.input)
			assert.Equal(t, tt.expected, i)
		})
	}
}

func TestInterfaceToInt64(t *testing.T) {
	i, err := InterfaceToInt64E(nil)
	assert.Nil(t, err)
	assert.Equal(t, int64(0), i)

	jsonN := "9223372036854775807"
	i, err = InterfaceToInt64E(jsonN)
	assert.Nil(t, err)
	assert.Equal(t, int64(9223372036854775807), i)
	jsonN = "-9223372036854775808"
	i, err = InterfaceToInt64E(jsonN)
	assert.Nil(t, err)
	assert.Equal(t, int64(-9223372036854775808), i)

	for _, tt := range numericTests {
		t.Run(tt.name, func(t *testing.T) {
			i, err = InterfaceToInt64E(tt.input)
			assert.Equal(t, int64(tt.expected), i)
		})
	}
}

func TestInterfaceToUint64(t *testing.T) {
	u, err := InterfaceToUint64E(nil)
	assert.Nil(t, err)
	assert.Equal(t, uint64(0), u)

	jsonN := "9223372036854775807"
	u, err = InterfaceToUint64E(jsonN)
	assert.Nil(t, err)
	assert.Equal(t, uint64(9223372036854775807), u)
	jsonN = "-9223372036854775808"
	u, err = InterfaceToUint64E(jsonN)
	assert.NotNil(t, err)
	assert.Equal(t, uint64(0), u)

	for _, tt := range numericTests {
		t.Run(tt.name, func(t *testing.T) {
			u, err = InterfaceToUint64E(tt.input)
			if tt.expected < 0 {
				assert.NotNil(t, err)
				assert.Equal(t, uint64(0), u)
			} else {
				assert.Equal(t, uint64(tt.expected), u)
			}
		})
	}
}
