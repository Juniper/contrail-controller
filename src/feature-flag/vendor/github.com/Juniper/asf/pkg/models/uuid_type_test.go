package models

import (
	"encoding/json"
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestUUIDType(t *testing.T) {
	uuidType := NewUUIDType("beefbeef-beef-beef-beef-beefbeef0002")
	b, err := json.Marshal(uuidType)
	assert.NoError(t, err)
	assert.Equal(t, "{\"uuid_mslong\":13758425323549998831,\"uuid_lslong\":13758425323549949954}", string(b))
	var decoded UuidType
	err = json.Unmarshal(b, &decoded)
	assert.NoError(t, err)
	assert.Equal(t, uint64(0xbeefbeefbeef0002), decoded.UUIDLslong)
	assert.Equal(t, uint64(0xbeefbeefbeefbeef), decoded.UUIDMslong)
}
