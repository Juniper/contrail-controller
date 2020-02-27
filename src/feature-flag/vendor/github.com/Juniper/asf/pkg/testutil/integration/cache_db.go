package integration

import (
	"context"

	"github.com/Juniper/asf/pkg/etcd"

	integrationetcd "github.com/Juniper/asf/pkg/testutil/integration/etcd"
	//TODO(mlastawiecki): uncomment once the file compiles
	//                    commented due to go mod errors
	//"github.com/Juniper/asf/pkg/constants"
	//"github.com/Juniper/asf/pkg/cache"
)

const (
	maxHistory = 100000
)

// RunCacheDB runs DB Cache with etcd event producer.
func RunCacheDB() (*cache.DB, func() error, error) {
	setViper(map[string]interface{}{
		"cache.timeout":           "10s",
		constants.ETCDEndpointsVK: []string{integrationetcd.Endpoint},
	})

	cacheDB := cache.NewDB(maxHistory)

	processor, err := etcd.NewEventProducer(cacheDB, "integration-cache-db")
	if err != nil {
		return nil, func() error { return nil }, err
	}

	ctx, cancelEtcdEventProducer := context.WithCancel(context.Background())
	errChan := make(chan error)
	go func() {
		errChan <- processor.Start(ctx)
	}()

	return cacheDB, func() error {
		cancelEtcdEventProducer()
		return <-errChan
	}, nil
}
