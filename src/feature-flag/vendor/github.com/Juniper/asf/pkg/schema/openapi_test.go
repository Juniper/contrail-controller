package schema

import (
	"fmt"
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestOpenAPI(t *testing.T) {
	api, err := MakeAPI([]string{"test_data/simple/schema"}, nil, false)
	assert.Nil(t, err, "API reading failed")
	fmt.Println(api)
	_, err = api.ToOpenAPI()
	assert.Nil(t, err, "OpenAPI generation failed")
}
