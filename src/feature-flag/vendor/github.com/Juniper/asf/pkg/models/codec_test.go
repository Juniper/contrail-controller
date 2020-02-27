package models

import (
	"fmt"
	"testing"

	proto "github.com/gogo/protobuf/proto"
	"github.com/gogo/protobuf/types"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/Juniper/asf/pkg/models/basemodels"
)

func TestUpdateData(t *testing.T) {
	codecs := []Codec{
		JSONCodec,
		ProtoCodec,
	}

	tests := []struct {
		name        string
		old, update basemodels.Object
		fm          types.FieldMask
		want        basemodels.Object
		fails       bool
	}{
		{name: "empty"},
		{
			name: "empty vn",
			old:  &VirtualNetwork{},
			want: &VirtualNetwork{},
		},
		{
			name:   "empty vn with empty update",
			old:    &VirtualNetwork{},
			update: &VirtualNetwork{},
			want:   &VirtualNetwork{},
		},
		{
			name:   "empty fieldmask",
			old:    &VirtualNetwork{UUID: "old-uuid", Name: "old-name"},
			update: &VirtualNetwork{UUID: "new-uuid"},
			want:   &VirtualNetwork{UUID: "old-uuid", Name: "old-name"},
		},
		{
			name:   "set UUID",
			old:    &VirtualNetwork{UUID: "old-uuid", Name: "old-name", DisplayName: "old-dn"},
			update: &VirtualNetwork{UUID: "new-uuid", DisplayName: "new-dn"},
			fm:     types.FieldMask{Paths: []string{"uuid"}},
			want:   &VirtualNetwork{UUID: "new-uuid", Name: "old-name", DisplayName: "old-dn"},
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			for _, c := range codecs {
				t.Run(fmt.Sprintf("%T", c), func(t *testing.T) {
					var oldData []byte
					var err error
					if tt.old != nil {
						oldData, err = c.Encode(tt.old)
						require.NoError(t, err)
					}

					gotData, err := UpdateData(c, oldData, tt.update, tt.fm)
					if tt.fails {
						assert.Error(t, err)
					} else {
						assert.NoError(t, err)
					}

					var got proto.Message
					if tt.old != nil {
						got = proto.Clone(tt.old)
						err = c.Decode(gotData, got)
						require.NoError(t, err)
					}
					assert.Equal(t, tt.want, got)
				})
			}
		})
	}
}
