package basedb

import (
	"context"
	"database/sql"
	"database/sql/driver"
	"fmt"
	"strings"
	"time"

	"github.com/ExpansiveWorlds/instrumentedsql"
	"github.com/Juniper/asf/pkg/errutil"
	"github.com/gogo/protobuf/proto"
	"github.com/lib/pq"
	"github.com/pkg/errors"
	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
)

// Database driver
const (
	DriverPostgreSQL = "postgres"

	dbDSNFormatPostgreSQL = "sslmode=disable user=%s password=%s host=%s dbname=%s"
)

//BaseDB struct for base function.
type BaseDB struct {
	db            *sql.DB
	Dialect       Dialect
	QueryBuilders map[string]*QueryBuilder
}

//NewBaseDB makes new base db instance.
func NewBaseDB(db *sql.DB) BaseDB {
	return BaseDB{
		db:      db,
		Dialect: NewDialect(),
	}
}

//DB gets db object.
func (db *BaseDB) DB() *sql.DB {
	return db.db
}

//Close closes db.
func (db *BaseDB) Close() error {
	if err := db.db.Close(); err != nil {
		return errors.Wrap(err, "close DB handle")
	}
	return nil
}

// Object is generic database model instance.
type Object interface {
	proto.Message
	ToMap() map[string]interface{}
}

// ObjectWriter processes rows
type ObjectWriter interface {
	WriteObject(schemaID, objUUID string, obj Object) error
}

//Transaction is a context key for tx object.
var Transaction interface{} = "transaction"

//GetTransaction get a transaction from context.
func GetTransaction(ctx context.Context) *sql.Tx {
	iTx := ctx.Value(Transaction)
	tx, _ := iTx.(*sql.Tx) //nolint: errcheck
	return tx
}

//DoInTransaction runs a function inside of DB transaction.
func (db *BaseDB) DoInTransaction(ctx context.Context, do func(context.Context) error) error {
	return db.DoInTransactionWithOpts(ctx, do, nil)
}

//DoInTransactionWithOpts runs a function inside of DB transaction with extra options.
func (db *BaseDB) DoInTransactionWithOpts(
	ctx context.Context, do func(context.Context) error, opts *sql.TxOptions,
) error {
	tx := GetTransaction(ctx)
	if tx != nil {
		return do(ctx)
	}

	conn, err := db.DB().Conn(ctx)
	if err != nil {
		return errors.Wrap(err, "failed to retrieve DB connection")
	}
	defer conn.Close() // nolint: errcheck

	tx, err = conn.BeginTx(ctx, opts)
	if err != nil {
		return errors.Wrap(FormatDBError(err), "failed to start DB transaction")
	}
	defer rollbackOnPanic(tx)

	err = do(context.WithValue(ctx, Transaction, tx))
	if err != nil {
		tx.Rollback() // nolint: errcheck
		return err
	}

	err = tx.Commit()
	if err != nil {
		tx.Rollback() // nolint: errcheck
		return FormatDBError(err)
	}

	return nil
}

// DoWithoutConstraints executes function without checking DB constraints
func (db *BaseDB) DoWithoutConstraints(ctx context.Context, do func(context.Context) error) (err error) {
	if err = db.disableConstraints(); err != nil {
		return err
	}
	defer func() {
		if enerr := db.enableConstraints(); enerr != nil {
			if err != nil {
				err = errutil.MultiError{err, enerr}
				return
			}
			err = enerr
		}
	}()
	err = do(ctx)
	return err
}

// disableConstraints globally disables constraints checking in DB - USE WITH CAUTION!
func (db *BaseDB) disableConstraints() error {
	_, err := db.DB().Exec(db.Dialect.DisableConstraints())
	return errors.Wrapf(err, "Disabling constraints checking (%s): ", db.Dialect.DisableConstraints())
}

// enableConstraints globally enables constraints checking - reverts behavior of DisableConstraints()
func (db *BaseDB) enableConstraints() error {
	_, err := db.DB().Exec(db.Dialect.EnableConstraints())
	return errors.Wrapf(err, "Enabling constraints checking (%s): ", db.Dialect.EnableConstraints())
}

func rollbackOnPanic(tx *sql.Tx) {
	if p := recover(); p != nil {
		err := tx.Rollback()
		if err != nil {
			panic(fmt.Sprintf("%v; also transaction rollback failed: %v", p, err))
		}
		panic(p)
	}
}

type DriverWrapper func(driver.Driver) driver.Driver

func WithInstrumentedSQL() func(driver.Driver) driver.Driver {
	return func(d driver.Driver) driver.Driver {
		return instrumentedsql.WrapDriver(d, instrumentedsql.WithLogger(instrumentedsql.LoggerFunc(logQuery)))
	}
}

//ConnectDB connect to the db based on viper configuration.
func ConnectDB(wrappers ...DriverWrapper) (*sql.DB, error) {
	if debug := viper.GetBool("database.debug"); debug {
		wrappers = append(wrappers, WithInstrumentedSQL())
	}

	db, err := OpenConnection(ConnectionConfig{
		DriverWrappers: wrappers,
		User:           viper.GetString("database.user"),
		Password:       viper.GetString("database.password"),
		Host:           viper.GetString("database.host"),
		Name:           viper.GetString("database.name"),
	})
	if err != nil {
		return nil, err
	}

	maxConn := viper.GetInt("database.max_open_conn")
	db.SetMaxOpenConns(maxConn)
	db.SetMaxIdleConns(maxConn)

	retries, period := viper.GetInt("database.connection_retries"), viper.GetDuration("database.retry_period")
	for i := 0; i < retries; i++ {
		err = db.Ping()
		if err == nil {
			logrus.Debug("Connected to the database")
			return db, nil
		}
		time.Sleep(period)
		logrus.WithError(err).Debug("DB connection error. Retrying...")
	}
	return nil, fmt.Errorf("failed to open DB connection")
}

// ConnectionConfig holds DB connection configuration.
type ConnectionConfig struct {
	DriverWrappers []DriverWrapper
	User           string
	Password       string
	Host           string
	Name           string
}

// OpenConnection opens DB connection.
func OpenConnection(c ConnectionConfig) (*sql.DB, error) {
	dsn, err := dataSourceName(&c)
	if err != nil {
		return nil, err
	}

	driverName := registerDriver(c.DriverWrappers)

	db, err := sql.Open(driverName, dsn)
	if err != nil {
		return nil, errors.Wrap(err, "failed to open DB connection")
	}
	return db, nil
}

func logQuery(_ context.Context, command string, args ...interface{}) {
	logrus.Debug(command, args)
}

func registerDriver(wrappers []DriverWrapper) string {
	driverName := "wrapped-" + DriverPostgreSQL

	if !isDriverRegistered(driverName) {
		var d driver.Driver = &pq.Driver{}
		for _, w := range wrappers {
			d = w(d)
		}
		sql.Register(driverName, d)
	}

	return driverName
}

func isDriverRegistered(driver string) bool {
	for _, d := range sql.Drivers() {
		if d == driver {
			return true
		}
	}
	return false
}

func dataSourceName(c *ConnectionConfig) (string, error) {
	return fmt.Sprintf(dbDSNFormatPostgreSQL, c.User, c.Password, c.Host, c.Name), nil
}

// Structure describes fields in schema.
type Structure map[string]interface{}

func (s *Structure) getPaths(prefix string) []string {
	var paths []string
	for k, v := range *s {
		p := prefix + "." + k
		switch o := v.(type) {
		case struct{}:
			paths = append(paths, p)
		case *Structure:
			paths = append(paths, o.getPaths(p)...)
		}
	}
	return paths
}

// GetInnerPaths gets all child for given fieldMask.
func (s *Structure) GetInnerPaths(fieldMask string) (paths []string) {
	innerStructure := s
	for _, segment := range strings.Split(fieldMask, ".") {
		switch o := (*innerStructure)[segment].(type) {
		case *Structure:
			innerStructure = o
		default:
			return nil
		}
	}
	return innerStructure.getPaths(fieldMask)
}
