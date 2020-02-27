package basedb

import (
	"testing"

	"github.com/stretchr/testify/assert"

	"github.com/Juniper/asf/pkg/models/basemodels"
)

func TestBuildMetadataFilter(t *testing.T) {
	tests := []struct {
		name           string
		args           []*basemodels.Metadata
		want           string
		expectedFilter string
		fails          bool
	}{
		{
			name: "Get multiple metadatas using UUID and FQName",
			args: []*basemodels.Metadata{
				{
					UUID: "uuid-b",
				},
				{
					FQName: []string{"default", "uuid-c"},
					Type:   "hoge",
				},
			},
			expectedFilter: " ( uuid = $1 )  or  ( type = $2 and fq_name = $3 ) ",
		},
		{
			name: "Get multiple metadatas using UUIDs",
			args: []*basemodels.Metadata{
				{
					UUID: "uuid-b",
				},
				{
					UUID: "uuid-c",
				},
			},
			expectedFilter: " ( uuid = $1 )  or  ( uuid = $2 ) ",
		},
		{
			name: "Get multiple metadatas using FQNames",
			args: []*basemodels.Metadata{
				{
					FQName: []string{"default", "uuid-b"},
					Type:   "hoge",
				},
				{
					FQName: []string{"default", "uuid-c"},
					Type:   "hoge",
				},
			},
			expectedFilter: " ( type = $1 and fq_name = $2 )  or  ( type = $3 and fq_name = $4 ) ",
		},
		{
			name: "Provide only FQNames - fail",
			args: []*basemodels.Metadata{
				{
					FQName: []string{"default", "uuid-b"},
				},
				{
					FQName: []string{"default", "uuid-c"},
				},
			},
		},
		{
			name: "Get metadata using FQName",
			args: []*basemodels.Metadata{
				{
					FQName: []string{"default", "uuid-b"},
					Type:   "hoge",
				},
			},
			expectedFilter: " ( type = $1 and fq_name = $2 ) ",
		},
		{
			name: "Get metadata using UUID",
			args: []*basemodels.Metadata{
				{
					UUID: "uuid-b",
				},
			},
			expectedFilter: " ( uuid = $1 ) ",
		},

		{
			name: "Get single metadata using UUID and FQName",
			args: []*basemodels.Metadata{
				{
					UUID: "uuid-b",
				},
				{
					FQName: []string{"default", "uuid-b"},
					Type:   "hoge",
				},
			},
			expectedFilter: " ( uuid = $1 )  or  ( type = $2 and fq_name = $3 ) ",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			f, _, err := buildMetadataFilter(NewDialect(), tt.args)

			if tt.fails {
				assert.Error(t, err)
				return
			}
			assert.Equal(t, tt.expectedFilter, f)
		})
	}
}
