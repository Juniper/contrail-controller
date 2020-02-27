package services

import (
	"encoding/json"
	"fmt"
	"testing"

	"github.com/stretchr/testify/assert"
	yaml "gopkg.in/yaml.v2"

	"github.com/Juniper/asf/pkg/fileutil"
	"github.com/Juniper/asf/pkg/models"
)

func TestCreateEventJSONEncoding(t *testing.T) {
	e := &Event{
		Request: &Event_CreateVirtualNetworkRequest{
			CreateVirtualNetworkRequest: &CreateVirtualNetworkRequest{
				VirtualNetwork: &models.VirtualNetwork{
					UUID: "vn_uuid",
				},
			},
		},
	}
	m, err := json.Marshal(e)
	assert.NoError(t, err, "marshal event failed")
	var i map[string]interface{}
	err = json.Unmarshal(m, &i)
	assert.NoError(t, err, "unmarshal event failed")
	assert.Equal(t, "virtual_network", i["kind"])
	assert.Equal(t, "CREATE", i["operation"])

	var d Event
	err = json.Unmarshal(m, &d)
	assert.NoError(t, err, "unmarshal event failed")
	request := d.GetCreateVirtualNetworkRequest()
	assert.Equal(t, "vn_uuid", request.GetVirtualNetwork().GetUUID())

	d2, err := NewEvent(EventOption{
		Kind:      i["kind"].(string),
		Operation: i["operation"].(string),
		Data:      i["data"].(map[string]interface{}),
	})
	if assert.NoError(t, err) {
		request = d2.GetCreateVirtualNetworkRequest()
		assert.Equal(t, "vn_uuid", request.GetVirtualNetwork().GetUUID())
	}
}

func TestDeleteEventJSONEncoding(t *testing.T) {
	e := &Event{
		Request: &Event_DeleteVirtualNetworkRequest{
			DeleteVirtualNetworkRequest: &DeleteVirtualNetworkRequest{
				ID: "vn_uuid",
			},
		},
	}
	m, err := json.Marshal(e)
	assert.NoError(t, err, "marshal event failed")
	fmt.Println(string(m))
	var i map[string]interface{}
	err = json.Unmarshal(m, &i)
	assert.NoError(t, err, "unmarshal event failed")
	assert.Equal(t, "virtual_network", i["kind"])
	assert.Equal(t, "DELETE", i["operation"])
	assert.Equal(t, "vn_uuid", i["data"].(map[string]interface{})["uuid"])

	var d Event
	err = json.Unmarshal(m, &d)
	assert.NoError(t, err, "unmarshal event failed")
	request := d.GetDeleteVirtualNetworkRequest()
	assert.Equal(t, "vn_uuid", request.ID)
}

func TestCreateEventYAMLEncoding(t *testing.T) {
	e := &Event{
		Request: &Event_CreateVirtualNetworkRequest{
			CreateVirtualNetworkRequest: &CreateVirtualNetworkRequest{
				VirtualNetwork: &models.VirtualNetwork{
					UUID: "vn_uuid",
				},
			},
		},
	}
	m, err := yaml.Marshal(e)
	fmt.Println(string(m))
	assert.NoError(t, err, "marshal event failed")

	var i map[string]interface{}
	err = yaml.Unmarshal(m, &i)
	assert.NoError(t, err, "unmarshal event failed")
	assert.Equal(t, "virtual_network", i["kind"])
	assert.Equal(t, "CREATE", i["operation"])

	var d Event
	err = yaml.UnmarshalStrict(m, &d)
	assert.NoError(t, err, "unmarshal event failed")
	request := d.GetCreateVirtualNetworkRequest()
	assert.Equal(t, "vn_uuid", request.GetVirtualNetwork().GetUUID())
	i = fileutil.YAMLtoJSONCompat(i).(map[string]interface{}) //nolint: errcheck
	d2, err := NewEvent(EventOption{
		Kind:      i["kind"].(string),
		Operation: i["operation"].(string),
		Data:      fileutil.YAMLtoJSONCompat(i["data"]).(map[string]interface{}),
	})
	if assert.NoError(t, err) {
		request = d2.GetCreateVirtualNetworkRequest()
		assert.Equal(t, "vn_uuid", request.GetVirtualNetwork().GetUUID())
	}
}

func TestSortEventList(t *testing.T) {
	tests := []struct {
		name        string
		events      []*Event
		sortedOrder []string
	}{
		{
			name:        "no events",
			events:      []*Event{},
			sortedOrder: []string{},
		},
		{
			name: "single event",
			events: []*Event{
				projectCreateEvent("project_uuid", ""),
			},
			sortedOrder: []string{"project_uuid"},
		},
		{
			name: "test two resources with the same parent",
			events: []*Event{
				projectCreateEvent("project_uuid_1", "domain_uuid"),
				projectCreateEvent("project_uuid_2", "domain_uuid"),
				domainCreateEvent("domain_uuid"),
			},
			sortedOrder: []string{"domain_uuid", "project_uuid_1", "project_uuid_2"},
		},
		{
			name: "test two resources with non existing parent (in event list)",
			events: []*Event{
				projectCreateEvent("project_uuid_1", "non_existing_uuid"),
				projectCreateEvent("project_uuid_2", "non_existing_uuid"),
			},
			sortedOrder: []string{"project_uuid_1", "project_uuid_2"},
		},
		{
			name: "test line parent-child structure",
			events: []*Event{
				routingInstanceCreateEvent("ri_uuid", "vi_uuid"),
				projectCreateEvent("project_uuid", "domain_uuid"),
				bgpRouterCreateEvent("bgp_uuid", "ri_uuid"),
				domainCreateEvent("domain_uuid"),
				virtualNetworkCreateEvent("vi_uuid", "project_uuid"),
			},
			sortedOrder: []string{"domain_uuid", "project_uuid", "vi_uuid", "ri_uuid", "bgp_uuid"},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			eventList := EventList{tt.events}
			err := eventList.Sort()
			assert.NoError(t, err)
			for i, e := range eventList.Events {
				assert.Equal(t, tt.sortedOrder[i], e.GetResource().GetUUID())
			}
		})
	}
}

func domainCreateEvent(uuid string) *Event {
	return &Event{
		Request: &Event_CreateDomainRequest{
			&CreateDomainRequest{
				Domain: &models.Domain{
					UUID: uuid,
				},
			},
		},
	}
}

func projectCreateEvent(uuid, parentUUID string) *Event {
	return &Event{
		Request: &Event_CreateProjectRequest{
			&CreateProjectRequest{
				Project: &models.Project{
					UUID:       uuid,
					ParentUUID: parentUUID,
				},
			},
		},
	}
}

func virtualNetworkCreateEvent(uuid, parentUUID string) *Event {
	return &Event{
		Request: &Event_CreateVirtualNetworkRequest{
			&CreateVirtualNetworkRequest{
				VirtualNetwork: &models.VirtualNetwork{
					UUID:       uuid,
					ParentUUID: parentUUID,
				},
			},
		},
	}
}

func routingInstanceCreateEvent(uuid, parentUUID string) *Event {
	return &Event{
		Request: &Event_CreateRoutingInstanceRequest{
			&CreateRoutingInstanceRequest{
				RoutingInstance: &models.RoutingInstance{
					UUID:       uuid,
					ParentUUID: parentUUID,
				},
			},
		},
	}
}

func bgpRouterCreateEvent(uuid, parentUUID string) *Event {
	return &Event{
		Request: &Event_CreateBGPRouterRequest{
			&CreateBGPRouterRequest{
				BGPRouter: &models.BGPRouter{
					UUID:       uuid,
					ParentUUID: parentUUID,
				},
			},
		},
	}
}
