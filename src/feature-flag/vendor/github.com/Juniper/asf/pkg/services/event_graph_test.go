package services

import (
	"testing"

	"github.com/stretchr/testify/assert"

	"github.com/Juniper/asf/pkg/models"
	"github.com/Juniper/asf/pkg/models/basemodels"
)

var projectEvent = &Event{
	Request: &Event_CreateProjectRequest{
		CreateProjectRequest: &CreateProjectRequest{
			Project: &models.Project{
				UUID: "Project",
			},
		},
	},
}

var vnEvent = &Event{
	Request: &Event_CreateVirtualNetworkRequest{
		CreateVirtualNetworkRequest: &CreateVirtualNetworkRequest{
			VirtualNetwork: &models.VirtualNetwork{
				UUID:       "VirtualNetwork",
				ParentUUID: "Project",
				NetworkIpamRefs: []*models.VirtualNetworkNetworkIpamRef{
					{UUID: "NetworkIpam"},
				},
			},
		},
	},
}

var ipamEvent = &Event{
	Request: &Event_CreateNetworkIpamRequest{
		CreateNetworkIpamRequest: &CreateNetworkIpamRequest{
			NetworkIpam: &models.NetworkIpam{
				UUID:       "NetworkIpam",
				ParentUUID: "Project",
			},
		},
	},
}

var fippEvent = &Event{
	Request: &Event_CreateFloatingIPPoolRequest{
		CreateFloatingIPPoolRequest: &CreateFloatingIPPoolRequest{
			FloatingIPPool: &models.FloatingIPPool{
				UUID:       "FloatingIPPool",
				ParentUUID: "VirtualNetwork",
			},
		},
	},
}

func TestCycle(t *testing.T) {
	projectWithRefEvent := &Event{
		Request: &Event_CreateProjectRequest{
			CreateProjectRequest: &CreateProjectRequest{
				Project: &models.Project{
					UUID: "Project",
					FloatingIPPoolRefs: []*models.ProjectFloatingIPPoolRef{
						{UUID: "FloatingIPPool"},
					},
				},
			},
		},
	}

	var tests = []struct {
		name    string
		events  []*Event
		isCycle bool
	}{
		{
			name: "no cycle",
			events: []*Event{
				projectEvent,
				vnEvent,
				fippEvent,
			},
		},
		{
			name:    "basic cycle",
			isCycle: true,
			events: []*Event{
				projectWithRefEvent,
				vnEvent,
				fippEvent,
			},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			refMap := make(map[*Event]basemodels.References)
			for _, ev := range tt.events {
				refMap[ev] = ev.getReferences()
			}
			graph := NewEventGraph(tt.events, refMap)
			assert.Equal(t, tt.isCycle, graph.HasCycle())
		})
	}
}

func TestSort(t *testing.T) {

	var tests = []struct {
		name     string
		events   []*Event
		expected *EventList
	}{
		{
			name: "sort basic",
			events: []*Event{
				projectEvent,
				vnEvent,
				ipamEvent,
				fippEvent,
			},
			expected: &EventList{
				Events: []*Event{
					projectEvent,
					ipamEvent,
					vnEvent,
					fippEvent,
				},
			},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			refMap := make(map[*Event]basemodels.References)
			for _, ev := range tt.events {
				refMap[ev] = ev.getReferences()
			}
			graph := NewEventGraph(tt.events, refMap)
			assert.Equal(t, tt.expected, graph.SortEvents())
		})
	}
}

func TestOperationKind(t *testing.T) {
	createEvent, err := NewEvent(EventOption{
		Operation: OperationCreate,
		Kind:      models.KindProject,
	})
	assert.NoError(t, err)

	updateEvent, err := NewEvent(EventOption{
		Operation: OperationUpdate,
		Kind:      models.KindProject,
	})
	assert.NoError(t, err)

	deleteEvent, err := NewEvent(EventOption{
		Operation: OperationDelete,
		Kind:      models.KindProject,
	})
	assert.NoError(t, err)

	var tests = []struct {
		name     string
		expected string
		events   *EventList
	}{
		{
			name:     "Multiple create events",
			expected: OperationCreate,
			events: &EventList{
				Events: []*Event{
					createEvent,
					createEvent,
					createEvent,
				},
			},
		},
		{
			name:     "Multiple update events",
			expected: OperationUpdate,
			events: &EventList{
				Events: []*Event{
					updateEvent,
					updateEvent,
					updateEvent,
				},
			},
		},
		{
			name:     "Multiple delete events",
			expected: OperationDelete,
			events: &EventList{
				Events: []*Event{
					deleteEvent,
					deleteEvent,
					deleteEvent,
				},
			},
		},
		{
			name:     "Mixed events",
			expected: "MIXED",
			events: &EventList{
				Events: []*Event{
					createEvent,
					updateEvent,
					deleteEvent,
				},
			},
		},
		{
			name:     "Empty event list",
			expected: "EMPTY",
			events: &EventList{
				Events: []*Event{},
			},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			assert.Equal(t, tt.events.OperationType(), tt.expected)
		})
	}
}

func TestIsSortRequired(t *testing.T) {
	projectWithParent := &Event{
		Request: &Event_CreateProjectRequest{
			CreateProjectRequest: &CreateProjectRequest{
				Project: &models.Project{
					UUID:       "Project",
					ParentUUID: "beefbeef-beef-beef-beef-beefbeef0002",
				},
			},
		},
	}
	projectWithExistingRef := &Event{
		Request: &Event_CreateProjectRequest{
			CreateProjectRequest: &CreateProjectRequest{
				Project: &models.Project{
					UUID: "Project",
					ApplicationPolicySetRefs: []*models.ProjectApplicationPolicySetRef{
						{
							UUID: "8a05c096-09ed-4c4b-a763-cf1d5ba92a27",
						},
					},
				},
			},
		},
	}
	virtualNetworkWithExistingParent := &models.VirtualNetwork{
		UUID:       "VirtualNetwork",
		ParentUUID: "beefbeef-beef-beef-beef-beefbeef0003",
	}
	virtualNetwork := &Event{
		Request: &Event_CreateVirtualNetworkRequest{
			CreateVirtualNetworkRequest: &CreateVirtualNetworkRequest{
				VirtualNetwork: &models.VirtualNetwork{
					UUID:       "VirtualNetwork",
					ParentUUID: "Project",
				},
			},
		},
	}

	tests := []struct {
		name        string
		events      []*Event
		requireSort bool
		include     bool
	}{
		{
			name:   "No events",
			events: []*Event{},
		},
		{
			name: "One event create without references",
			events: []*Event{
				projectEvent,
			},
		},
		{
			name: "One event create with already existing parent",
			events: []*Event{
				projectWithParent,
			},
		},
		{
			name: "One event create with already existing reference",
			events: []*Event{
				projectWithExistingRef,
			},
		},
		{
			name: "Two independent create events",
			events: []*Event{
				projectWithExistingRef,
				{
					Request: &Event_CreateVirtualNetworkRequest{
						CreateVirtualNetworkRequest: &CreateVirtualNetworkRequest{
							VirtualNetwork: virtualNetworkWithExistingParent,
						},
					},
				},
			},
		},
		{
			name: "Two parent-child dependent create events in right order",
			events: []*Event{
				projectWithExistingRef,
				virtualNetwork,
			},
		},
		{
			name: "Two parent-child dependent create events in wrong order",
			events: []*Event{
				virtualNetwork,
				projectWithExistingRef,
			},
			requireSort: true,
		},
		{
			name: "Three reference dependent (with refs only to themselves) create events in right order",
			events: []*Event{
				projectEvent,
				ipamEvent,
				vnEvent,
			},
		},
		{
			name: "Three reference dependent (with refs only to themselves) create events in wrong order",
			events: []*Event{
				vnEvent,
				projectEvent,
				ipamEvent,
			},
			requireSort: true,
		},
		{
			name: "Three reference dependent (mixed) create events in right order",
			events: []*Event{
				projectWithParent,
				ipamEvent,
				vnEvent,
			},
		},
		{
			name: "Three reference dependent (mixed) create events in wrong order",
			events: []*Event{
				vnEvent,
				projectWithParent,
				ipamEvent,
			},
			requireSort: true,
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			refMap := make(map[*Event]basemodels.References)
			for _, ev := range tt.events {
				refMap[ev] = ev.getReferences()
			}
			g := NewEventGraph(tt.events, refMap)
			list := &EventList{
				Events: tt.events,
			}
			assert.Equal(t, tt.requireSort, g.IsSortRequired(list, refMap))
		})
	}
}
