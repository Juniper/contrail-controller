package replication

import (
	"context"
	"fmt"

	"github.com/kyleconroy/pgoutput"
	"github.com/sirupsen/logrus"

	"github.com/Juniper/asf/pkg/logutil"
	"github.com/Juniper/asf/pkg/services"
)

// PgoutputHandler handles replication messages by decoding them as events and passing them to processor.
type PgoutputHandler struct {
	decoder   EventDecoder
	processor eventListProcessor
	log       *logrus.Entry

	txnInProgress *transaction

	relations relationSet
}

type relationSet map[uint32]pgoutput.Relation

// NewPgoutputHandler creates new ReplicationEventHandler with provided decoder and processor.
func NewPgoutputHandler(p eventListProcessor, d EventDecoder) *PgoutputHandler {
	return &PgoutputHandler{
		decoder:   d,
		processor: p,
		log:       logutil.NewLogger("replication-event-handler"),
		relations: relationSet{},
	}
}

// Handle handles provided message by passing decoding its contents passing them to processor.
func (h *PgoutputHandler) Handle(ctx context.Context, msg pgoutput.Message) error {

	switch v := msg.(type) {
	case pgoutput.Relation:
		h.log.Debug("received RELATION message")
		h.relations[v.ID] = v
	case pgoutput.Begin:
		h.log.Debug("received BEGIN message")
		h.txnInProgress = beginTransaction(h.processor)
	case pgoutput.Commit:
		h.log.Debug("received COMMIT message")
		return h.txnInProgress.Commit(ctx)
	case pgoutput.Insert:
		h.log.Debug("received INSERT message")
		return h.handleDataEvent(ctx, services.OperationCreate, v.RelationID, v.Row)
	case pgoutput.Update:
		h.log.Debug("received UPDATE message")
		return h.handleDataEvent(ctx, services.OperationUpdate, v.RelationID, v.Row)
	case pgoutput.Delete:
		h.log.Debug("received DELETE message")
		return h.handleDataEvent(ctx, services.OperationDelete, v.RelationID, v.Row)
	}
	return nil
}

func (h *PgoutputHandler) handleDataEvent(
	ctx context.Context, operation string, relationID uint32, row []pgoutput.Tuple,
) error {
	relation, ok := h.relations[relationID]
	if !ok {
		return fmt.Errorf("no relation for %d", relationID)
	}

	pk, data, err := decodeRowData(relation, row)
	if err != nil {
		return fmt.Errorf("error decoding row: %v", err)
	}
	if len(pk) == 0 {
		return fmt.Errorf("no primary key specified for row: %v", row)
	}

	ev, err := h.decoder.DecodeRowEvent(operation, relation.Name, pk, data)
	if err != nil {
		return err
	}

	return h.process(ctx, ev)
}

func (h *PgoutputHandler) process(ctx context.Context, e *services.Event) error {
	if h.txnInProgress == nil {
		_, err := h.processor.ProcessList(ctx, &services.EventList{Events: []*services.Event{e}})
		return err
	}

	h.txnInProgress.add(e)
	return nil
}

func decodeRowData(
	relation pgoutput.Relation,
	row []pgoutput.Tuple,
) (pk []string, data map[string]interface{}, err error) {
	keys, data := []interface{}{}, map[string]interface{}{}

	if t, c := len(row), len(relation.Columns); t != c {
		return nil, nil, fmt.Errorf("malformed message or relation columns, got %d values but relation has %d columns", t, c)
	}

	for i, tuple := range row {
		col := relation.Columns[i]
		decoder := col.Decoder()
		if err = decoder.DecodeText(nil, tuple.Value); err != nil {
			return nil, nil, fmt.Errorf("error decoding column '%v': %s", col.Name, err)
		}
		value := decoder.Get()
		data[col.Name] = value
		if col.Key {
			keys = append(keys, value)
		}

	}

	pk, err = primaryKeyToStringSlice(keys)
	if err != nil {
		return nil, nil, fmt.Errorf("error creating PK: %v", err)
	}

	return pk, data, nil
}

func primaryKeyToStringSlice(keyValues []interface{}) ([]string, error) {
	keys := []string{}
	for i, pk := range keyValues {
		if pk == nil || pk == "" {
			return nil, fmt.Errorf("primary key value is nil or empty on key element at index %v", i)
		}
		keys = append(keys, fmt.Sprint(pk))
	}
	return keys, nil
}
