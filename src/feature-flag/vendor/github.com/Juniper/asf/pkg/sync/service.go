// Package sync contains functionality that supplies etcd with data from PostgreSQL database.
package sync

import (
	"context"

	//"github.com/Juniper/asf/pkg/constants"
	"github.com/Juniper/asf/pkg/db"
	"github.com/Juniper/asf/pkg/db/basedb"
	"github.com/Juniper/asf/pkg/etcd"
	"github.com/Juniper/asf/pkg/logutil"
	"github.com/Juniper/asf/pkg/models"
	"github.com/Juniper/asf/pkg/services"
	"github.com/Juniper/asf/pkg/sync/replication"
	"github.com/jackc/pgx"
	"github.com/pkg/errors"
	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
)

const (
	syncID = "sync-service"
)

type watchCloser interface {
	Watch(context.Context) error
	DumpDone() <-chan struct{}
	Close()
}

type eventProcessor interface {
	services.EventProcessor
	ProcessList(context.Context, *services.EventList) (*services.EventList, error)
}

// Service represents Sync service.
type Service struct {
	watcher watchCloser
	log     *logrus.Entry
}

// NewService creates Sync service with given configuration.
// Close needs to be explicitly called on service teardown.
func NewService() (*Service, error) {

	if err := logutil.Configure(viper.GetString("log_level")); err != nil {
		return nil, err
	}

	c := determineCodecType()
	if c == nil {
		return nil, errors.New(`unknown codec set as "sync.storage"`)
	}

	etcdNotifierService, err := etcd.NewNotifierService(viper.GetString(constants.ETCDPathVK), c)
	if err != nil {
		return nil, err
	}

	watcher, err := createWatcher(syncID, &services.EventListProcessor{
		EventProcessor:    NewFQNameCache(&services.ServiceEventProcessor{Service: etcdNotifierService}),
		InTransactionDoer: etcdNotifierService.Client,
	})
	if err != nil {
		return nil, err
	}

	return &Service{
		watcher: watcher,
		log:     logutil.NewLogger(syncID),
	}, nil
}

func setViperDefaults() {
	viper.SetDefault("log_level", "debug")
	viper.SetDefault(constants.ETCDDialTimeoutVK, "60s")
	viper.SetDefault("database.retry_period", "1s")
	viper.SetDefault("database.connection_retries", 10)
	viper.SetDefault("database.replication_status_timeout", "10s")
}

func determineCodecType() models.Codec {
	switch viper.GetString("sync.storage") {
	case models.JSONCodec.Key():
		return models.JSONCodec
	case models.ProtoCodec.Key():
		return models.ProtoCodec
	default:
		return nil
	}
}

func createWatcher(id string, processor eventProcessor) (watchCloser, error) {
	setViperDefaults()
	sqlDB, err := basedb.ConnectDB()
	if err != nil {
		return nil, err
	}

	dbService := db.NewService(sqlDB)
	if err != nil {
		return nil, err
	}

	return createPostgreSQLWatcher(id, dbService, processor)
}

func createPostgreSQLWatcher(
	id string, dbService *db.Service, processor eventProcessor,
) (watchCloser, error) {
	handler := replication.NewPgoutputHandler(processor, dbService)

	connConfig := pgx.ConnConfig{
		Host:     viper.GetString("database.host"),
		Database: viper.GetString("database.name"),
		User:     viper.GetString("database.user"),
		Password: viper.GetString("database.password"),
	}

	replConn, err := pgx.ReplicationConnect(connConfig)
	if err != nil {
		return nil, err
	}
	conf := replication.PostgresSubscriptionConfig{
		Slot:          replication.SlotName(id),
		Publication:   replication.PostgreSQLPublicationName,
		StatusTimeout: viper.GetDuration("database.replication_status_timeout"),
	}

	return replication.NewPostgresWatcher(
		conf,
		dbService,
		replConn,
		handler.Handle,
		processor,
		viper.GetBool("sync.dump"),
	)
}

// Run runs Sync service.
func (s *Service) Run() error {
	s.log.Info("Running Sync service")
	return s.watcher.Watch(context.Background())
}

// DumpDone returns a channel that is closed when dump is done.
func (s *Service) DumpDone() <-chan struct{} {
	return s.watcher.DumpDone()
}

// Close closes Sync service.
func (s *Service) Close() {
	s.log.Info("Closing Sync service")
	s.watcher.Close()
}
