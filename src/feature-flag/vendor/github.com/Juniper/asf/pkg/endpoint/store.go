package endpoint

import (
	"strings"
	"sync"

	"github.com/Juniper/asf/pkg/models"
)

// Endpoint related constants.
const (
	PublicURLScope  = "public"
	PrivateURLScope = "private"
)

// TargetStore is used to store service specific endpoint targets in-memory.
type TargetStore struct {
	Data       *sync.Map
	nextTarget string
}

// NewTargetStore returns new TargetStore.
func NewTargetStore() *TargetStore {
	return &TargetStore{
		Data:       new(sync.Map),
		nextTarget: "",
	}
}

// Read endpoint target from memory.
func (t *TargetStore) Read(id string) *models.Endpoint {
	rawE, ok := t.Data.Load(id)
	if !ok {
		return nil
	}
	e, ok := rawE.(*models.Endpoint)
	if !ok {
		return nil
	}
	return e
}

// ReadAll reads all endpoint targets from target store using given scope.
// Order of returned targets is not deterministic.
func (t *TargetStore) ReadAll(scope string) (targets []*Endpoint) {
	if scope != PublicURLScope && scope != PrivateURLScope {
		return nil
	}

	t.Data.Range(func(id, endpoint interface{}) bool {
		e, ok := endpoint.(*models.Endpoint)
		if !ok {
			return true
		}

		targets = append(targets, newTargetEndpoint(e, scope))
		return true
	})
	return targets
}

func newTargetEndpoint(e *models.Endpoint, scope string) *Endpoint {
	switch scope {
	case PublicURLScope:
		return NewEndpoint(e.PublicURL, e.Username, e.Password)
	case PrivateURLScope:
		if e.PrivateURL != "" {
			return NewEndpoint(e.PrivateURL, e.Username, e.Password)
		}
		return NewEndpoint(e.PublicURL, e.Username, e.Password)
	}
	return nil
}

// Write writes endpoint target in-memory.
func (t *TargetStore) Write(id string, endpoint *models.Endpoint) {
	t.Data.Store(id, endpoint)
}

// Next reads next endpoint target from memory in round robin fashion.
func (t *TargetStore) Next(scope string) (endpointData *Endpoint) {
	t.Data.Range(func(id, endpoint interface{}) bool {
		ids := id.(string) // nolint: errcheck
		if t.nextTarget == "" {
			t.nextTarget = ids
		}
		if endpointData != nil {
			t.nextTarget = ids
			// exit Range iteration as next target is identified
			return false
		}
		switch scope {
		case PublicURLScope:
			if ids == t.nextTarget {
				e := endpoint.(*models.Endpoint) // nolint: errcheck
				endpointData = NewEndpoint(e.PublicURL, e.Username, e.Password)
				// let Range iterate till nextServer is identified
				return true
			}
		case PrivateURLScope:
			if ids == t.nextTarget {
				e := endpoint.(*models.Endpoint) // nolint: errcheck
				endpointData = NewEndpoint(e.PrivateURL, e.Username, e.Password)
				if endpointData == nil {
					// no private url configured, use public url
					e := endpoint.(*models.Endpoint) // nolint: errcheck
					endpointData = NewEndpoint(e.PublicURL, e.Username, e.Password)
				}
				// let Range iterate till nextServer is identified
				return true
			}
		}
		return true
	})
	return endpointData
}

// Remove removes endpoint target from memory.
func (t *TargetStore) Remove(endpointKey string) {
	if endpointKey == t.nextTarget {
		// Reset the next target before deleting the endpoint
		t.nextTarget = ""
	}
	t.Data.Delete(endpointKey)
}

// Count returns number of endpoint targets in memory.
func (t *TargetStore) Count() int {
	count := 0
	t.Data.Range(func(id, endpoint interface{}) bool {
		count++
		return true
	})
	return count
}

// Store is used to store cluster specific endpoint targets store in-memory.
type Store struct {
	Data *sync.Map
}

// NewStore returns new Store.
func NewStore() *Store {
	return &Store{
		Data: new(sync.Map),
	}
}

// Read reads endpoint targets store from memory.
func (e *Store) Read(endpointKey string) *TargetStore {
	p, ok := e.Data.Load(endpointKey)
	if !ok {
		return nil
	}

	ts, _ := p.(*TargetStore) // nolint: errcheck
	return ts
}

// Write writes endpoint targets store in-memory.
func (e *Store) Write(endpointKey string, endpointStore *TargetStore) {
	e.Data.Store(endpointKey, endpointStore)
}

// Remove removes endpoint target store from memory.
func (e *Store) Remove(prefix string) {
	e.Data.Delete(prefix)
}

// GetEndpoint returns endpoint.
func (e *Store) GetEndpoint(clusterID, prefix string) (endpoint *Endpoint) {
	if clusterID == "" {
		return nil
	}
	// TODO(dfurman): "server.dynamic_proxy_path" or DefaultDynamicProxyPath should be used
	targets := e.Read(strings.Join([]string{"/proxy", clusterID, prefix, PrivateURLScope}, "/"))
	if targets == nil {
		return nil
	}
	return targets.Next(PrivateURLScope)
}

// Endpoint represents an endpoint url with its credentials.
type Endpoint struct {
	URL      string
	Username string
	Password string
}

// NewEndpoint returns new Endpoint.
func NewEndpoint(url, user, password string) *Endpoint {
	return &Endpoint{
		URL:      url,
		Username: user,
		Password: password,
	}
}
