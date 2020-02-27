package replication

import (
	"context"
	"testing"

	"github.com/jackc/pgx/pgtype"
	"github.com/kyleconroy/pgoutput"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/mock"

	"github.com/Juniper/asf/pkg/services"
)

func TestPgOutputEventHandlerHandle(t *testing.T) {
	exampleRelation := pgoutput.Relation{
		Name: "test-resource",
		Columns: []pgoutput.Column{
			{Name: "string-property", Key: true, Type: pgtype.VarcharOID},
			{Name: "int-property", Type: pgtype.Int4OID},
			{Name: "float-property", Type: pgtype.Float8OID},
		},
	}

	exampleRow := []pgoutput.Tuple{
		{Value: []byte(`foo`)},
		{Value: []byte(`1337`)},
		{Value: []byte(`1.337`)},
	}

	exampleRowData := map[string]interface{}{
		"string-property": "foo",
		"int-property":    int32(1337),
		"float-property":  1.337,
	}

	tests := []struct {
		name         string
		initMock     func(oner)
		initialRels  relationSet
		message      pgoutput.Message
		fails        bool
		expectedRels relationSet
	}{
		{name: "nil message", message: nil},
		{name: "insert unknown relation", message: pgoutput.Insert{}, fails: true},
		{name: "update unknown relation", message: pgoutput.Update{}, fails: true},
		{name: "delete unknown relation", message: pgoutput.Delete{}, fails: true},
		{name: "insert malformed relation", message: pgoutput.Insert{RelationID: 1}, fails: true},
		{name: "update malformed relation", message: pgoutput.Update{RelationID: 1}, fails: true},
		{name: "delete malformed relation", message: pgoutput.Delete{RelationID: 1}, fails: true},
		{
			name:        "insert no primary key",
			initialRels: relationSet{1: pgoutput.Relation{Name: "rel"}},
			message:     pgoutput.Insert{RelationID: 1},
			fails:       true,
		},
		{
			name:        "update no primary key",
			initialRels: relationSet{1: pgoutput.Relation{Name: "rel"}},
			message:     pgoutput.Update{RelationID: 1},
			fails:       true,
		},
		{
			name:        "delete no primary key",
			initialRels: relationSet{1: pgoutput.Relation{Name: "rel"}},
			message:     pgoutput.Delete{RelationID: 1},
			fails:       true,
		},
		{
			name:         "new relation",
			message:      pgoutput.Relation{ID: 1337},
			expectedRels: relationSet{1337: pgoutput.Relation{ID: 1337}},
		},
		{
			name:         "already stored relation",
			initialRels:  relationSet{1337: pgoutput.Relation{Name: "old"}},
			message:      pgoutput.Relation{ID: 1337, Name: "new"},
			expectedRels: relationSet{1337: pgoutput.Relation{ID: 1337, Name: "new"}},
		},
		{
			name: "correct insert message",
			initMock: func(m oner) {
				m.On("DecodeRowEvent", services.OperationCreate, "test-resource", []string{"foo"}, exampleRowData).Return(
					&services.Event{Version: 1}, nil,
				).Once()
			},
			initialRels: relationSet{1: exampleRelation},
			message:     pgoutput.Insert{RelationID: 1, Row: exampleRow},
		},
		{
			name: "correct update message",
			initMock: func(m oner) {
				m.On("DecodeRowEvent", services.OperationUpdate, "test-resource", []string{"foo"}, exampleRowData).Return(
					&services.Event{Version: 1}, nil,
				).Once()
			},
			initialRels: relationSet{1: exampleRelation},
			message:     pgoutput.Update{RelationID: 1, Row: exampleRow},
		},
		{
			name: "correct delete message",
			initMock: func(m oner) {
				m.On("DecodeRowEvent", services.OperationDelete, "test-resource", []string{"foo"}, exampleRowData).Return(
					&services.Event{Version: 1}, nil,
				).Once()
			},
			initialRels: relationSet{1: exampleRelation},
			message:     pgoutput.Delete{RelationID: 1, Row: exampleRow},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			// given
			m := &rowSinkMock{}
			if tt.initMock != nil {
				tt.initMock(m)
			}
			pm := &eventProcessorMock{}

			h := NewPgoutputHandler(pm, m)
			if tt.initialRels != nil {
				h.relations = tt.initialRels
			}

			// when
			err := h.Handle(context.Background(), tt.message)

			// then
			if tt.fails {
				assert.Error(t, err)
			} else {
				assert.NoError(t, err)
			}

			if tt.expectedRels != nil {
				assert.Equal(t, tt.expectedRels, h.relations)
			}

			m.AssertExpectations(t)
		})
	}
}

type rowSinkMock struct {
	mock.Mock
}

func (m *rowSinkMock) DecodeRowEvent(
	operation, resourceName string, pk []string, properties map[string]interface{},
) (*services.Event, error) {
	args := m.Called(operation, resourceName, pk, properties)
	return args.Get(0).(*services.Event), args.Error(1)
}

type eventProcessorMock struct {
}

func (m *eventProcessorMock) Process(ctx context.Context, e *services.Event) (*services.Event, error) {
	return nil, nil
}

func (m *eventProcessorMock) ProcessList(ctx context.Context, e *services.EventList) (*services.EventList, error) {
	return nil, nil
}
