package replication

import (
	"context"
	"database/sql"
	"fmt"
	"io"
	"strings"

	"github.com/jackc/pgx"
	"github.com/pkg/errors"
	"github.com/sirupsen/logrus"

	"github.com/Juniper/asf/pkg/db/basedb"
	"github.com/Juniper/asf/pkg/logutil"
)

type pgxReplicationConn interface {
	io.Closer

	DropReplicationSlot(slotName string) (err error)
	CreateReplicationSlotEx(slotName, outputPlugin string) (consistentPoint string, snapshotName string, err error)
	SendStandbyStatus(k *pgx.StandbyStatus) (err error)
	StartReplication(slotName string, startLsn uint64, timeline int64, pluginArguments ...string) (err error)
	WaitForReplicationMessage(ctx context.Context) (*pgx.ReplicationMessage, error)
}

type dbService interface {
	DB() *sql.DB
	DoInTransactionWithOpts(ctx context.Context, do func(context.Context) error, opts *sql.TxOptions) error
}

type postgresReplicationConnection struct {
	replConn pgxReplicationConn
	db       dbService
	log      *logrus.Entry
}

func newPostgresReplicationConnection(
	db dbService, replConn pgxReplicationConn,
) (*postgresReplicationConnection, error) {
	return &postgresReplicationConnection{
		db:       db,
		replConn: replConn,
		log:      logutil.NewLogger("postgres-replication-connection"),
	}, nil
}

// GetReplicationSlot gets replication slot for replication.
func (c *postgresReplicationConnection) GetReplicationSlot(
	name string,
) (maxWal uint64, snapshotName string, err error) {
	if dropErr := c.replConn.DropReplicationSlot(name); err != nil {
		c.log.WithError(dropErr).Info("Could not drop replication slot just before getting new one - safely ignoring")
	}

	// If creating the replication slot fails with code 42710, this means
	// the replication slot already exists.
	consistentPoint, snapshotName, err := c.replConn.CreateReplicationSlotEx(name, "pgoutput")
	if err != nil {
		if pgerr, ok := err.(pgx.PgError); !ok || pgerr.Code != "42710" {
			return 0, "", errors.Wrap(err, "failed to create replication slot")
		}
	}

	maxWal, err = pgx.ParseLSN(consistentPoint)
	if err != nil {
		return 0, "", fmt.Errorf("error parsing received LSN: %v", err)
	}
	return maxWal, snapshotName, err
}

// RenewPublication ensures that publication exists for all tables.
func (c *postgresReplicationConnection) RenewPublication(ctx context.Context, name string) error {
	return c.db.DoInTransactionWithOpts(
		ctx,
		func(ctx context.Context) error {
			_, err := c.db.DB().ExecContext(ctx, fmt.Sprintf("DROP PUBLICATION IF EXISTS %s", name))
			if err != nil {
				return errors.Wrap(err, "failed to drop publication")
			}
			_, err = c.db.DB().ExecContext(ctx, fmt.Sprintf("CREATE PUBLICATION %s FOR ALL TABLES", name))
			if err != nil {
				return errors.Wrap(err, "failed to create publication")
			}
			return err
		},
		nil,
	)
}

// IsInRecovery checks is database server is in recovery mode.
func (c *postgresReplicationConnection) IsInRecovery(ctx context.Context) (isInRecovery bool, err error) {
	return isInRecovery, c.db.DoInTransactionWithOpts(
		ctx,
		func(ctx context.Context) error {
			r, err := c.db.DB().QueryContext(ctx, "SELECT pg_is_in_recovery()")
			if err != nil {
				return errors.Wrap(err, "failed to check recovery mode")
			}
			if !r.Next() {
				return errors.New("pg_is_in_recovery() returned zero rows")
			}
			if err := r.Scan(&isInRecovery); err != nil {
				return errors.Wrap(err, "error scanning recovery status")
			}
			return nil
		},
		&sql.TxOptions{ReadOnly: true},
	)
}

func (c *postgresReplicationConnection) DoInTransactionSnapshot(
	ctx context.Context,
	snapshotName string,
	do func(context.Context) error,
) error {
	return c.db.DoInTransactionWithOpts(
		ctx,
		func(ctx context.Context) error {
			tx := basedb.GetTransaction(ctx)
			_, err := tx.ExecContext(ctx, "SET TRANSACTION ISOLATION LEVEL REPEATABLE READ")
			if err != nil {
				return errors.Wrap(err, "error setting transaction isolation")
			}
			_, err = tx.ExecContext(ctx, fmt.Sprintf("SET TRANSACTION SNAPSHOT '%s'", snapshotName))
			if err != nil {
				return errors.Wrap(err, "error setting transaction snapshot")
			}

			return do(ctx)
		},
		&sql.TxOptions{ReadOnly: true},
	)
}

func pluginArgs(publication string) string {
	return fmt.Sprintf(`("proto_version" '1', "publication_names" '%s')`, publication)
}

// StartReplication sends start replication message to server.
func (c *postgresReplicationConnection) StartReplication(slot, publication string, startLSN uint64) error {
	// timeline argument should be -1 otherwise postgres reutrns error - pgx library bug
	return c.replConn.StartReplication(slot, startLSN, -1, pluginArgs(publication))
}

// WaitForReplicationMessage blocks until message arrives on replication connection.
func (c *postgresReplicationConnection) WaitForReplicationMessage(
	ctx context.Context,
) (*pgx.ReplicationMessage, error) {
	return c.replConn.WaitForReplicationMessage(ctx)
}

// SendStatus sends standby status to server connected with replication connection.
func (c *postgresReplicationConnection) SendStatus(receivedLSN, savedLSN uint64) error {
	k, err := pgx.NewStandbyStatus(
		savedLSN,    // flush - savedLSN is already stored in etcd so we can say that it's flushed
		savedLSN,    // apply - savedLSN is stored and visible in etcd so it's also applied
		receivedLSN, // write - receivedLSN is last wal segment that was received by watcher
	)
	if err != nil {
		return errors.Wrap(err, "error creating standby status")
	}
	if err = c.replConn.SendStandbyStatus(k); err != nil {
		return errors.Wrap(err, "failed to send standy status")
	}
	return nil
}

// Close closes underlying connections.
func (c *postgresReplicationConnection) Close() error {
	var errs []string
	if dbErr := c.db.DB().Close(); dbErr != nil {
		errs = append(errs, dbErr.Error())
	}
	if replConnErr := c.replConn.Close(); replConnErr != nil {
		errs = append(errs, replConnErr.Error())
	}
	if len(errs) > 0 {
		return fmt.Errorf("errors while closing: %s", strings.Join(errs, "\n"))
	}
	return nil
}
