package sync

import (
	"context"

	"github.com/sirupsen/logrus"

	"github.com/Juniper/asf/pkg/logutil"
)

// EventProducer based on RDBMS updates.
type EventProducer struct {
	Watcher watchCloser
	log     *logrus.Entry
}

// NewEventProducer makes EventProducer based RDBMS updates.
// Every EventProducer must have a unique id.
func NewEventProducer(id string, processor eventProcessor) (*EventProducer, error) {
	Watcher, err := createWatcher(id, processor)
	if err != nil {
		return nil, err
	}
	return &EventProducer{
		log:     logutil.NewLogger("sync-event-producer"),
		Watcher: Watcher,
	}, nil
}

// Start runs EventProducer.
func (e *EventProducer) Start(ctx context.Context) error {
	e.log.Info("Running Sync service")
	return e.Watcher.Watch(ctx)
}

// Close closes EventProducer.
func (e *EventProducer) Close() {
	e.log.Info("Closing Sync service")
	e.Watcher.Close()
}
