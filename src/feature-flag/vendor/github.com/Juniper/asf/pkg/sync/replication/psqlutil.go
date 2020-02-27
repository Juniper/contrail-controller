package replication

import (
	"context"
	"strings"
	"sync"

	"github.com/Juniper/asf/pkg/services"
)

const (
	// PostgreSQLPublicationName contains name of publication created in database.
	PostgreSQLPublicationName = "syncpub"
)

// SlotName transforms watcher ID to replication slot name.
func SlotName(id string) string {
	return strings.Replace(id, "-", "_", -1)
}

type eventListProcessor interface {
	ProcessList(context.Context, *services.EventList) (*services.EventList, error)
}

type transaction struct {
	events    services.EventList
	processor eventListProcessor
}

func beginTransaction(p eventListProcessor) *transaction {
	return &transaction{
		processor: p,
	}
}

func (t *transaction) add(e *services.Event) {
	t.events.Events = append(t.events.Events, e)
}

func (t *transaction) Commit(ctx context.Context) error {
	if t == nil {
		return nil
	}
	_, err := t.processor.ProcessList(ctx, &t.events)
	return err
}

type lsnCounter struct {
	// receivedLSN holds max WAL LSN value received from master.
	receivedLSN uint64

	// savedLSN holds max WAL LSN value saved in etcd.
	// We can also assume that savedLSN <= receivedLSN.
	savedLSN uint64

	txnsInProgress int

	m sync.Mutex
}

func (c *lsnCounter) updateReceivedLSN(lsn uint64) {
	c.m.Lock()
	defer c.m.Unlock()

	if c.receivedLSN < lsn {
		c.receivedLSN = lsn
	}
}

func (c *lsnCounter) lsnValues() (receivedLSN, savedLSN uint64) {
	c.m.Lock()
	defer c.m.Unlock()

	if c.txnsInProgress == 0 {
		c.savedLSN = c.receivedLSN
	}

	return c.receivedLSN, c.savedLSN
}

func (c *lsnCounter) txnStarted() {
	c.m.Lock()
	defer c.m.Unlock()

	c.txnsInProgress++
}

func (c *lsnCounter) txnFinished(processedLSN uint64) {
	c.m.Lock()
	defer c.m.Unlock()

	if c.savedLSN < processedLSN {
		c.savedLSN = processedLSN
	}

	if c.txnsInProgress > 0 {
		c.txnsInProgress--
	}
}
