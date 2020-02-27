package services

import (
	"encoding/json"
	"reflect"
	"testing"

	"github.com/stretchr/testify/assert"

	"github.com/Juniper/asf/pkg/models"
)

func Test_newCollectionItem(t *testing.T) {
	tests := []struct {
		name    string
		obj     interface{}
		field   string
		want    interface{}
		wantErr bool
	}{
		{
			name:  "resolve KeyValuePair type",
			obj:   models.VirtualNetwork{},
			field: "annotations",
			want:  &models.KeyValuePair{},
		},
		{
			name:  "pointer type",
			obj:   &models.VirtualNetwork{},
			field: "annotations",
			want:  &models.KeyValuePair{},
		},
		{
			name:    "non collection field",
			obj:     models.VirtualNetwork{},
			field:   "uuid",
			wantErr: true,
		},
		{
			name:    "invalid field",
			obj:     models.VirtualNetwork{},
			field:   "bad_field_that_does_not_exist",
			wantErr: true,
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got, err := newCollectionItem(tt.obj, tt.field)
			if tt.wantErr {
				assert.Error(t, err)
			} else {
				assert.NoError(t, err)
			}
			assert.Equal(t, tt.want, got)
		})
	}
}

func Test_fieldByTag(t *testing.T) {
	tests := []struct {
		name   string
		t      reflect.Type
		key    string
		value  string
		want   reflect.StructField
		wantOk bool
	}{
		{
			name: "virtual network's annotations field",
			t:    reflect.TypeOf(models.VirtualNetwork{}),
			key:  "json", value: "annotations",
			want:   fieldByName(models.VirtualNetwork{}, "Annotations"),
			wantOk: true,
		},
		{
			name: "annotations field from pointer type",
			t:    reflect.TypeOf(&models.VirtualNetwork{}),
			key:  "json", value: "annotations",
			want:   fieldByName(models.VirtualNetwork{}, "Annotations"),
			wantOk: true,
		},
		{
			name: "field name typo",
			t:    reflect.TypeOf(models.VirtualNetwork{}),
			key:  "json", value: "annotatio",
			wantOk: false,
		},
		{
			name: "non existing tag",
			t:    reflect.TypeOf(models.VirtualNetwork{}),
			key:  "bad", value: "annotations",
			wantOk: false,
		},
		{
			name: "nil type",
			t:    reflect.TypeOf(nil),
			key:  "bad", value: "annotations",
			wantOk: false,
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got, got1 := fieldByTag(tt.t, tt.key, tt.value)
			assert.Equal(t, tt.want, got)
			assert.Equal(t, tt.wantOk, got1)
		})
	}
}

func fieldByName(x interface{}, name string) reflect.StructField {
	f, _ := reflect.TypeOf(x).FieldByName(name)
	return f
}

func Test_restPropCollectionUpdateRequest_toPropCollectionUpdateRequest(t *testing.T) {
	tests := []struct {
		name    string
		data    []byte
		obj     interface{}
		want    PropCollectionUpdateRequest
		wantErr bool
	}{
		{name: "empty", data: []byte(`{}`)},
		{
			name: "with empty update",
			data: []byte(`{"updates": [{}]}`),
			obj:  &VirtualNetwork{},
			want: PropCollectionUpdateRequest{
				Updates: []*PropCollectionChange{{}},
			},
		},
		{
			name: "with empty annotations update",
			data: []byte(`{"updates": [{"field": "annotations"}]}`),
			obj:  &VirtualNetwork{},
			want: PropCollectionUpdateRequest{
				Updates: []*PropCollectionChange{{
					Field: "annotations",
				}},
			},
		},
		{
			name: "annotations",
			data: []byte(
				`{"updates": [{
					"position": "some-key",
					"field": "annotations",
					"operation": "set",
					"value": {"key": "some-key", "value":"val"}
				}]}`,
			),
			obj: &VirtualNetwork{},
			want: PropCollectionUpdateRequest{
				Updates: []*PropCollectionChange{{
					Field:     "annotations",
					Operation: "set",
					Value: &PropCollectionChange_KeyValuePairValue{KeyValuePairValue: &models.KeyValuePair{
						Key: "some-key", Value: "val",
					}},
					Position: &PropCollectionChange_PositionString{PositionString: "some-key"},
				}},
			},
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			p := &restPropCollectionUpdateRequest{}

			err := json.Unmarshal(tt.data, &p)
			assert.NoError(t, err)

			got, err := p.toPropCollectionUpdateRequest(tt.obj)
			if tt.wantErr {
				assert.Error(t, err)
			} else {
				assert.NoError(t, err)
			}
			assert.Equal(t, tt.want, got)
		})
	}
}
