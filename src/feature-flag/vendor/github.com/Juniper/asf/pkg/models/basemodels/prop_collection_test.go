package basemodels

import (
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestPropCollectionUpdate_PositionForList(t *testing.T) {
	correctValue := []byte("value")

	tests := []struct {
		name             string
		update           *PropCollectionUpdate
		expectedPosition int
		fails            bool
	}{
		{
			name: "fails for add operation without value",
			update: &PropCollectionUpdate{
				Operation: PropCollectionUpdateOperationAdd,
			},
			fails: true,
		},
		{
			name: "returns position 0 for add operation",
			update: &PropCollectionUpdate{
				Operation: PropCollectionUpdateOperationAdd,
				Value:     correctValue,
			},
			expectedPosition: 0,
		},
		{
			name: "fails for modify operation without value",
			update: &PropCollectionUpdate{
				Operation: PropCollectionUpdateOperationModify,
			},
			fails: true,
		},
		{
			name: "fails for modify operation with invalid position",
			update: &PropCollectionUpdate{
				Operation: PropCollectionUpdateOperationModify,
				Value:     correctValue,
				Position:  "five",
			},
			fails: true,
		},
		{
			name: "returns position for modify operation",
			update: &PropCollectionUpdate{
				Operation: PropCollectionUpdateOperationModify,
				Value:     correctValue,
				Position:  int32(5),
			},
			expectedPosition: 5,
		},
		{
			name: "fails for delete operation with invalid position",
			update: &PropCollectionUpdate{
				Operation: PropCollectionUpdateOperationDelete,
				Value:     correctValue,
				Position:  "five",
			},
			fails: true,
		},
		{
			name: "returns position for delete operation",
			update: &PropCollectionUpdate{
				Operation: PropCollectionUpdateOperationDelete,
				Value:     correctValue,
				Position:  int32(5),
			},
			expectedPosition: 5,
		},
		{
			name: "fails for set operation",
			update: &PropCollectionUpdate{
				Operation: PropCollectionUpdateOperationSet,
			},
			fails: true,
		},
		{
			name: "fails for invalid operation",
			update: &PropCollectionUpdate{
				Operation: "invalid",
			},
			fails: true,
		},
		{
			name: "returns position for mixed case operation string",
			update: &PropCollectionUpdate{
				Operation: "aDd",
				Value:     correctValue,
			},
			expectedPosition: 0,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			position, err := tt.update.PositionForList()

			if tt.fails {
				assert.Error(t, err)
				assert.Equal(t, 0, position)
			} else {
				assert.NoError(t, err)
				assert.Equal(t, tt.expectedPosition, position)
			}
		})
	}
}

func TestPropCollectionUpdate_KeyForMap(t *testing.T) {
	correctValue := []byte("value")

	tests := []struct {
		name   string
		update *PropCollectionUpdate
		key    string
		fails  bool
	}{
		{
			name: "succeeds with correct key for delete",
			update: &PropCollectionUpdate{
				Operation: "delete",
				Value:     correctValue,
				Position:  string("key"),
			},
			key: "key",
		},
		{
			name: "fails for set operation without value",
			update: &PropCollectionUpdate{
				Operation: PropCollectionUpdateOperationSet,
			},
			fails: true,
		},
		{
			name: "fails for delete operation without position",
			update: &PropCollectionUpdate{
				Operation: PropCollectionUpdateOperationDelete,
				Position:  "",
			},
			fails: true,
		},
		{
			name: "succeeds for mixed case operation string",
			update: &PropCollectionUpdate{
				Operation: "sEt",
				Value:     correctValue,
			},
		},
		{
			name: "fails for add operation",
			update: &PropCollectionUpdate{
				Operation: PropCollectionUpdateOperationAdd,
				Value:     correctValue,
			},
			fails: true,
		},
		{
			name: "fails for modify operation",
			update: &PropCollectionUpdate{
				Operation: PropCollectionUpdateOperationModify,
				Value:     correctValue,
			},
			fails: true,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			key, err := tt.update.KeyForMap()

			if tt.fails {
				assert.Error(t, err)
			} else {
				assert.NoError(t, err)
			}

			assert.Equal(t, tt.key, key)
		})
	}
}
