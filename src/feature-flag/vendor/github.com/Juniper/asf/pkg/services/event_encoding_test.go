package services

import (
	"encoding/json"
	"testing"

	"github.com/gogo/protobuf/types"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
	yaml "gopkg.in/yaml.v2"

	"github.com/Juniper/asf/pkg/models"
)

func TestNewEvent(t *testing.T) {
	type args struct {
		option EventOption
	}
	tests := []struct {
		name    string
		args    args
		want    *Event
		wantErr bool
	}{
		{
			name: "try to create event with empty option",
			args: args{
				option: EventOption{},
			},
			wantErr: true,
		},
		{
			name: "create event with default (Create) operation",
			args: args{
				option: EventOption{
					Kind: models.KindProject,
				},
			},
			want: &Event{
				Request: &Event_CreateProjectRequest{
					CreateProjectRequest: &CreateProjectRequest{
						Project: &models.Project{},
					},
				},
			},
		},
		{
			name: "create event with Create operation",
			args: args{
				option: EventOption{
					Operation: OperationCreate,
					Kind:      models.KindProject,
					Data: map[string]interface{}{
						"name": "hoge",
					},
				},
			},
			want: &Event{
				Request: &Event_CreateProjectRequest{
					CreateProjectRequest: &CreateProjectRequest{
						Project: &models.Project{
							Name: "hoge",
						},
					},
				},
			},
		},
		{
			name: "create event with Update operation",
			args: args{
				option: EventOption{
					Operation: OperationUpdate,
					Kind:      models.KindProject,
					UUID:      "hoge",
					FieldMask: &types.FieldMask{
						Paths: []string{"name"},
					},
					Data: map[string]interface{}{
						"name": "hoge",
					},
				},
			},
			want: &Event{
				Request: &Event_UpdateProjectRequest{
					UpdateProjectRequest: &UpdateProjectRequest{
						Project: &models.Project{
							UUID: "hoge",
							Name: "hoge",
						},
						FieldMask: types.FieldMask{
							Paths: []string{"name"},
						},
					},
				},
			},
		},
		{
			name: "create event with Delete operation",
			args: args{
				option: EventOption{
					Operation: OperationDelete,
					Kind:      models.KindProject,
					UUID:      "hoge",
				},
			},
			want: &Event{
				Request: &Event_DeleteProjectRequest{
					DeleteProjectRequest: &DeleteProjectRequest{
						ID: "hoge",
					},
				},
			},
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got, err := NewEvent(tt.args.option)
			if tt.wantErr {
				assert.Error(t, err)
			} else {
				assert.NoError(t, err)
				assertEventsAreEqual(t, tt.want, got)
			}
		})
	}
}

func TestEvent_ToMap(t *testing.T) {
	tests := []struct {
		name    string
		request isEvent_Request
		want    map[string]interface{}
	}{
		{
			name: "empty event to map",
		},
		{
			name: "create event to map",
			request: &Event_CreateProjectRequest{
				CreateProjectRequest: &CreateProjectRequest{
					Project: &models.Project{
						UUID: "hoge",
					},
				},
			},
			want: map[string]interface{}{
				"kind":      models.KindProject,
				"operation": OperationCreate,
				"data": &Project{
					UUID: "hoge",
				},
			},
		},
		{
			name: "update event to map",
			request: &Event_UpdateProjectRequest{
				UpdateProjectRequest: &UpdateProjectRequest{
					Project: &models.Project{
						UUID: "hoge",
					},
				},
			},
			want: map[string]interface{}{
				"kind":      models.KindProject,
				"operation": OperationUpdate,
				"data": &Project{
					UUID: "hoge",
				},
			},
		},
		{
			name: "delete event to map",
			request: &Event_DeleteProjectRequest{
				DeleteProjectRequest: &DeleteProjectRequest{
					ID: "hoge",
				},
			},
			want: map[string]interface{}{
				"kind":      models.KindProject,
				"operation": OperationDelete,
				"data": map[string]interface{}{
					"uuid": "hoge",
				},
			},
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			e := &Event{
				Request: tt.request,
			}
			got := e.ToMap()
			assert.Equal(t, tt.want, got)
		})
	}
}

func TestEvent_UnmarshalJSON(t *testing.T) {
	tests := []struct {
		name  string
		data  []byte
		want  *Event
		fails bool
	}{
		{
			name: "basic project create",
			data: []byte(`{
					"kind": "project",
					"data": {
						"uuid":        "project_uuid",
						"name":		   "project_name",
						"fq_name":     ["default-domain", "project_name"],
						"parent_type": "domain"
					},
					"operation": "CREATE"
				   }`),
			want: &Event{
				Request: &Event_CreateProjectRequest{
					CreateProjectRequest: &CreateProjectRequest{
						Project: &models.Project{
							UUID:       "project_uuid",
							Name:       "project_name",
							ParentType: "domain",
							FQName:     []string{"default-domain", "project_name"},
						},
						FieldMask: types.FieldMask{
							Paths: []string{"uuid", "name", "fq_name", "parent_type"},
						},
					},
				},
			},
		},
		{
			name: "invalid resource kind",
			data: []byte(`{
					"kind": "hoge",
					"data": {
						"uuid":        "project_uuid",
						"name":		   "project_name",
						"fq_name":     ["default-domain", "project_name"],
						"parent_type": "domain"
					}
				   }`),
			fails: true,
		},
		{
			name: "invalid data",
			data: []byte(`{
					"kind": "project",
					"data": "hoge"
				   }`),
			fails: true,
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			ev := &Event{}
			ensureValidJSON(t, tt.data)
			err := json.Unmarshal(tt.data, &ev)
			if tt.fails {
				assert.Error(t, err)
			} else {
				assert.NoError(t, err)
				assert.Equal(t, tt.want.GetResource(), ev.GetResource())
			}
		})
	}
}

func TestEvent_UnmarshalYAML(t *testing.T) {
	tests := []struct {
		name  string
		data  []byte
		want  *Event
		fails bool
	}{
		{
			name: "basic project create",
			data: []byte(`---
kind: project
data:
  uuid: project_uuid
  name: project_name
  fq_name:
  - default-domain
  - project_name
  parent_type: domain
operation: CREATE
`),
			want: &Event{
				Request: &Event_CreateProjectRequest{
					CreateProjectRequest: &CreateProjectRequest{
						Project: &models.Project{
							UUID:       "project_uuid",
							Name:       "project_name",
							ParentType: "domain",
							FQName:     []string{"default-domain", "project_name"},
						},
						FieldMask: types.FieldMask{
							Paths: []string{"uuid", "name", "fq_name", "parent_type"},
						},
					},
				},
			},
		},
		{
			name: "invalid resource kind",
			data: []byte(`---
kind: hoge
data:
  uuid: project_uuid
  name: project_name
  fq_name:
  - default-domain
  - project_name
  parent_type: domain
`),
			fails: true,
		},
		{
			name: "invalid data",
			data: []byte(`---
kind: project
data: hoge
`),
			fails: true,
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			ev := &Event{}
			ensureValidYAML(t, tt.data)
			err := yaml.Unmarshal(tt.data, ev)
			if tt.fails {
				assert.Error(t, err)
			} else {
				assert.NoError(t, err)
				assert.Equal(t, tt.want.GetResource(), ev.GetResource(), "UnmarshalJSON() got:\n%v\nwant:\n%v", ev, tt.want)
			}
		})
	}
}

func TestEvent_ApplyMap(t *testing.T) {
	projectWithUUID := &models.Project{
		UUID: "hoge",
	}
	tests := []struct {
		name     string
		m        map[string]interface{}
		expected Event
		fails    bool
	}{
		{
			name:  "fail due to empty kind",
			fails: true,
		},
		{
			name: "fail due to empty data",
			m: map[string]interface{}{
				"kind": "project",
			},
			fails: true,
		},
		{
			name: "default (create) project event",
			m: map[string]interface{}{
				"kind": "project",
				"data": map[string]interface{}{
					"uuid": "hoge",
				},
			},
			expected: Event{
				Request: &Event_CreateProjectRequest{
					CreateProjectRequest: &CreateProjectRequest{
						Project: projectWithUUID,
					},
				},
			},
		},
		{
			name: "create project event",
			m: map[string]interface{}{
				"kind": "project",
				"data": map[string]interface{}{
					"uuid": "hoge",
				},
				"operation": OperationCreate,
			},
			expected: Event{
				Request: &Event_CreateProjectRequest{
					CreateProjectRequest: &CreateProjectRequest{
						Project: projectWithUUID,
					},
				},
			},
		},
		{
			name: "update project event",
			m: map[string]interface{}{
				"kind": "project",
				"data": map[string]interface{}{
					"uuid": "hoge",
				},
				"operation": OperationUpdate,
			},
			expected: Event{
				Request: &Event_UpdateProjectRequest{
					UpdateProjectRequest: &UpdateProjectRequest{
						Project: projectWithUUID,
					},
				},
			},
		},
		{
			name: "delete project event",
			m: map[string]interface{}{
				"kind": "project",
				"data": map[string]interface{}{
					"uuid": "hoge",
				},
				"operation": OperationDelete,
			},
			expected: Event{
				Request: &Event_DeleteProjectRequest{
					DeleteProjectRequest: &DeleteProjectRequest{
						ID: "hoge",
					},
				},
			},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := Event{}
			err := got.ApplyMap(tt.m)
			if tt.fails {
				assert.Error(t, err)
			} else {
				assert.NoError(t, err)
				assertEventsAreEqual(t, &tt.expected, &got)
			}
		})
	}
}

func assertEventsAreEqual(t *testing.T, expected *Event, actual *Event) {
	assert.Equal(t, expected.GetResource(), actual.GetResource())
	assert.Equal(t, expected.Kind(), actual.Kind())
	assert.Equal(t, expected.Operation(), actual.Operation())
	for _, p := range getFieldMask(expected).Paths {
		assert.Contains(t, getFieldMask(actual).Paths, p)
	}
}

func getFieldMask(e *Event) types.FieldMask {
	switch t := e.Request.(type) {
	case *Event_CreateProjectRequest:
		return t.CreateProjectRequest.FieldMask
	case *Event_UpdateProjectRequest:
		return t.UpdateProjectRequest.FieldMask
	default:
		return types.FieldMask{}
	}
}

func ensureValidJSON(t *testing.T, b []byte) {
	var i interface{}
	err := json.Unmarshal(b, &i)
	require.NoError(t, err)
}

func ensureValidYAML(t *testing.T, b []byte) {
	var i interface{}
	err := yaml.Unmarshal(b, &i)
	require.NoError(t, err)
}
