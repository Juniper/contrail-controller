package sync

import (
	"context"

	"github.com/pkg/errors"

	"github.com/Juniper/asf/pkg/models/basemodels"
	"github.com/Juniper/asf/pkg/services"
)

// FQNameCache event processor with fqName cache.
type FQNameCache struct {
	services.EventProcessor
	idToFQName fqNameCache
}

type fqNameCache map[string][]string

// NewFQNameCache returns new event processor with fqName cache.
func NewFQNameCache(p services.EventProcessor) *FQNameCache {
	return &FQNameCache{
		EventProcessor: p,
		idToFQName:     fqNameCache{},
	}
}

// Process updates cache, adds fqNames to refEvents and processes the event with the EventProcessor.
func (p *FQNameCache) Process(ctx context.Context, event *services.Event) (*services.Event, error) {
	p.updateFQNameCache(event)

	if err := p.sanitizeCreateRefEvent(event); err != nil {
		return nil, errors.Wrapf(err, "failed to sanitize reference fqName, event: %v", event)
	}

	return p.EventProcessor.Process(ctx, event)
}

func (p *FQNameCache) updateFQNameCache(event *services.Event) {
	switch request := event.Unwrap().(type) {
	case resourceRequest:
		r := request.GetResource()
		p.idToFQName[r.GetUUID()] = r.GetFQName()
	case deleteRequest:
		delete(p.idToFQName, request.GetID())
	}
}

type resourceRequest interface {
	GetResource() basemodels.Object
}

type deleteRequest interface {
	GetID() string
	Kind() string
}

func (p *FQNameCache) sanitizeCreateRefEvent(event *services.Event) error {
	refRequest, ok := event.Unwrap().(services.CreateRefRequest)
	if !ok {
		return nil
	}
	reference := refRequest.GetReference()
	fqName, ok := p.idToFQName[reference.GetUUID()]
	if !ok {
		return errors.Errorf("failed to fetched reference fq_name for uuid: '%v'", reference.GetUUID())
	}
	reference.SetTo(fqName)
	return nil
}
