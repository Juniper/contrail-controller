package services

import (
	"encoding/json"
	"testing"

	"github.com/Juniper/asf/pkg/models"
	"github.com/stretchr/testify/assert"
	yaml "gopkg.in/yaml.v2"
)

type testDataList struct {
	structure ListFloatingIPPoolResponse
	bytes     []byte
}

func TestListResponseJSON(t *testing.T) {
	for _, data := range []testDataList{
		{
			ListFloatingIPPoolResponse{
				FloatingIPPools:     []*models.FloatingIPPool{{UUID: "vn_uuid"}},
				FloatingIPPoolCount: 1,
			},
			[]byte(`{"floating-ip-pools": [{"uuid": "vn_uuid"}]}`),
		}, {
			ListFloatingIPPoolResponse{
				FloatingIPPools:     make([]*models.FloatingIPPool, 0),
				FloatingIPPoolCount: 0,
			},
			[]byte(`{"floating-ip-pools": []}`),
		},
	} {
		t.Run("marshaling", func(t *testing.T) {
			dataBytes, err := json.Marshal(data.structure.Data())

			assert.NoError(t, err, "marshaling ListResponse.Data() failed")
			assert.JSONEq(t, string(data.bytes), string(dataBytes))
		})

		t.Run("unmarshalling", func(t *testing.T) {
			var dataStruct ListFloatingIPPoolResponse

			err := json.Unmarshal(data.bytes, &dataStruct)

			assert.NoError(t, err, "unmarshaling ListResponse failed")
			assert.Equal(t, data.structure, dataStruct)
		})
	}
}

func TestListDetailedResponseJSONMarshaling(t *testing.T) {
	for _, data := range []testDataList{
		{
			ListFloatingIPPoolResponse{
				FloatingIPPools:     []*models.FloatingIPPool{{UUID: "vn_uuid"}},
				FloatingIPPoolCount: 1,
			},
			[]byte(`{"floating-ip-pools": [{"floating-ip-pool": {"uuid": "vn_uuid"}}]}`),
		}, {
			ListFloatingIPPoolResponse{
				FloatingIPPools:     make([]*models.FloatingIPPool, 0),
				FloatingIPPoolCount: 0,
			},
			[]byte(`{"floating-ip-pools": []}`),
		}, {
			ListFloatingIPPoolResponse{
				FloatingIPPools:     nil,
				FloatingIPPoolCount: 0,
			},
			[]byte(`{"floating-ip-pools": []}`),
		},
	} {
		dataBytes, err := json.Marshal(data.structure.Detailed())

		assert.NoError(t, err, "marshaling ListResponse.Detailed() failed")
		assert.JSONEq(t, string(data.bytes), string(dataBytes))
	}
}

func TestListCountResponseJSONMarshaling(t *testing.T) {
	for _, data := range []testDataList{
		{
			ListFloatingIPPoolResponse{
				FloatingIPPools:     []*models.FloatingIPPool{{UUID: "vn_uuid"}},
				FloatingIPPoolCount: 1,
			},
			[]byte(`{"floating-ip-pools": {"count": 1}}`),
		}, {
			ListFloatingIPPoolResponse{
				FloatingIPPools:     make([]*models.FloatingIPPool, 0),
				FloatingIPPoolCount: 0,
			},
			[]byte(`{"floating-ip-pools": {"count": 0}}`),
		}, {
			ListFloatingIPPoolResponse{
				FloatingIPPools:     nil,
				FloatingIPPoolCount: 0,
			},
			[]byte(`{"floating-ip-pools": {"count": 0}}`),
		},
	} {
		dataBytes, err := json.Marshal(data.structure.Count())

		assert.NoError(t, err, "marshaling ListResponse.Count() failed")
		assert.JSONEq(t, string(data.bytes), string(dataBytes))
	}
}

func TestListResponseYAML(t *testing.T) {
	for _, data := range []testDataList{
		{
			ListFloatingIPPoolResponse{
				FloatingIPPools: []*models.FloatingIPPool{
					{
						UUID: "vn_uuid",
					},
				},
				FloatingIPPoolCount: 1,
			},
			[]byte(`floating-ip-pools:
- uuid: vn_uuid
`),
		},
		{
			ListFloatingIPPoolResponse{
				FloatingIPPools:     make([]*models.FloatingIPPool, 0),
				FloatingIPPoolCount: 0,
			},
			[]byte(`floating-ip-pools: []
`),
		},
	} {
		t.Run("marshaling", func(t *testing.T) {
			dataBytes, err := yaml.Marshal(data.structure.Data())

			assert.NoError(t, err, "marshaling ListResponse failed")
			assert.Equal(t, data.bytes, dataBytes)
		})

		t.Run("unmarshalling", func(t *testing.T) {
			var dataStruct ListFloatingIPPoolResponse

			err := yaml.UnmarshalStrict(data.bytes, &dataStruct)

			assert.NoError(t, err, "unmarshaling ListResponse failed")
			assert.EqualValues(t, len(data.structure.FloatingIPPools), len(dataStruct.FloatingIPPools))
			for i := range data.structure.FloatingIPPools {
				assert.EqualValues(t, data.structure.FloatingIPPools[i], dataStruct.FloatingIPPools[i])
			}
		})
	}
}
