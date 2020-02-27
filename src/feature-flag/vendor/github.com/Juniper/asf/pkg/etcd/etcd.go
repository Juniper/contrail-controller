package etcd

import (
	"context"
	"fmt"
	"io/ioutil"
	"os"
	"path"
	"time"

	"github.com/Juniper/asf/pkg/logutil"
	"github.com/coreos/etcd/clientv3"
	"github.com/coreos/etcd/mvcc/mvccpb"
	"github.com/coreos/etcd/pkg/transport"
	"github.com/pkg/errors"
	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	"google.golang.org/grpc/grpclog"

	conc "github.com/coreos/etcd/clientv3/concurrency"
)

const (
	kvClientRequestTimeout = 60 * time.Second
)

// Viper keys
const (
	ETCDEndpointsVK          = "etcd.endpoints"
	ETCDDialTimeoutVK        = "etcd.dial_timeout"
	ETCDGRPCInsecureVK       = "etcd.grpc_insecure"
	ETCDPathVK               = "etcd.path"
)

// ResourceKey constructs key for given resource type and pk.
// TODO(dfurman): pass ETCDPathVK value instead of reading it from the global configuration.
func ResourceKey(resourceType, pk string) string {
	return path.Join("/", viper.GetString(ETCDPathVK), resourceType, pk)
}

// Client is an etcd client using clientv3.
type Client struct {
	ETCD *clientv3.Client
	log  *logrus.Entry
}

// Config holds Client configuration.
type Config struct {
	*clientv3.Client // optional clientv3.Client
	clientv3.Config  // config for new clientv3.Client to create
	TLSConfig        TLSConfig
	ServiceName      string
}

// TLSConfig holds Client TLS configuration.
type TLSConfig struct {
	Enabled         bool
	CertificatePath string
	KeyPath         string
	TrustedCAPath   string
}

// NewClient creates new etcd Client with given clientv3.Client.
// It creates new clientv3.Client if it is not passed by parameter.
func NewClient(c *Config) (*Client, error) {
	clientv3.SetLogger(grpclog.NewLoggerV2(ioutil.Discard, os.Stdout, os.Stdout))

	var etcd *clientv3.Client
	if c.Client != nil {
		etcd = c.Client
	} else {
		var err error
		etcd, err = newETCDClient(c)
		if err != nil {
			return nil, err
		}
	}

	return &Client{
		ETCD: etcd,
		log:  logutil.NewLogger(fmt.Sprint(c.ServiceName, "-etcd-client")),
	}, nil
}

func newETCDClient(c *Config) (*clientv3.Client, error) {
	if c.TLSConfig.Enabled {

		if c.TLSConfig.CertificatePath == "" || c.TLSConfig.KeyPath == "" || c.TLSConfig.TrustedCAPath == "" {
			return nil, errors.New("no TLS config")
		}

		var err error
		c.TLS, err = transport.TLSInfo{
			CertFile:      c.TLSConfig.CertificatePath,
			KeyFile:       c.TLSConfig.KeyPath,
			TrustedCAFile: c.TLSConfig.TrustedCAPath,
		}.ClientConfig()
		if err != nil {
			return nil, errors.Wrapf(err, "invalid TLS config")
		}
	}

	etcd, err := clientv3.New(c.Config)
	if err != nil {
		return nil, errors.Wrapf(err, "connecting to etcd failed")
	}

	return etcd, nil
}

// Get gets a value in etcd.
func (c *Client) Get(ctx context.Context, key string) ([]byte, error) {
	kvHandle := clientv3.NewKV(c.ETCD)
	response, err := kvHandle.Get(ctx, key)
	if err != nil || response.Count == 0 {
		return nil, err
	}
	return response.Kvs[0].Value, nil
}

// Put puts value in etcd no matter if it was there or not.
func (c *Client) Put(ctx context.Context, key string, value []byte) error {
	kvHandle := clientv3.NewKV(c.ETCD)

	_, err := kvHandle.Put(ctx, key, string(value))

	return err
}

// Create puts value in etcd if following key didn't exist.
func (c *Client) Create(ctx context.Context, key string, value []byte) error {
	kvHandle := clientv3.NewKV(c.ETCD)

	_, err := kvHandle.Txn(ctx).
		If(clientv3.Compare(clientv3.Version(key), "=", 0)).
		Then(clientv3.OpPut(key, string(value))).
		Commit()

	return err
}

// Update puts value in etcd if key existed before.
func (c *Client) Update(ctx context.Context, key, value string) error {
	kvHandle := clientv3.NewKV(c.ETCD)

	_, err := kvHandle.Txn(ctx).
		If(clientv3.Compare(clientv3.Version(key), "=", 0)).
		Else(clientv3.OpPut(key, value)).
		Commit()

	return err
}

// Delete deletes a key/value in etcd.
func (c *Client) Delete(ctx context.Context, key string) error {
	kvHandle := clientv3.NewKV(c.ETCD)

	_, err := kvHandle.Txn(ctx).
		If(clientv3.Compare(clientv3.Version(key), "=", 0)).
		Else(clientv3.OpDelete(key)).
		Commit()

	return err
}

// WatchRecursive watches a key pattern for changes After an Index and returns channel with messages.
func (c *Client) WatchRecursive(
	ctx context.Context, keyPattern string, afterIndex int64,
) chan Message {
	return c.Watch(ctx, keyPattern, clientv3.WithPrefix(), clientv3.WithRev(afterIndex))

}

// Watch watches a key and returns channel with messages.
func (c *Client) Watch(
	ctx context.Context, key string, opts ...clientv3.OpOption,
) chan Message {
	resultChan := make(chan Message)
	rchan := c.ETCD.Watch(ctx, key, opts...)

	go func() {
		for wresp := range rchan {
			for _, ev := range wresp.Events {
				resultChan <- NewMessage(ev)
			}
		}
		close(resultChan)
	}()

	return resultChan
}

// DoInTransaction wraps clientv3 transaction and wraps conc.STM with own Txn.
func (c *Client) DoInTransaction(ctx context.Context, do func(context.Context) error) error {
	if txn := GetTxn(ctx); txn != nil {
		// Transaction already in context
		return do(ctx)
	}
	// New transaction required

	ctx, cancel := context.WithTimeout(context.Background(), kvClientRequestTimeout)
	defer cancel()

	_, err := conc.NewSTM(c.ETCD, func(stm conc.STM) error {
		return do(WithTxn(ctx, StmTxn{stm, c.log}))
	}, conc.WithAbortContext(ctx))
	return err
}

// Close closes client.
func (c *Client) Close() error {
	return c.ETCD.Close()
}

// Message contains message data reveived from WatchRecursive.
type Message struct {
	Revision int64
	Type     int32
	Key      string
	Value    []byte
}

// Message type values.
const (
	MessageCreate = iota
	MessageModify
	MessageDelete
	MessageUnknown
)

// NewMessage creates a new message object based on Event.
func NewMessage(e *clientv3.Event) Message {
	return Message{
		Revision: e.Kv.ModRevision,
		Type:     messageTypeFromEvent(e),
		Key:      string(e.Kv.Key),
		Value:    e.Kv.Value,
	}
}

func messageTypeFromEvent(e *clientv3.Event) int32 {
	switch {
	case e.IsCreate():
		return MessageCreate
	case e.IsModify():
		return MessageModify
	case e.Type == mvccpb.DELETE:
		return MessageDelete
	}
	return MessageUnknown
}

// Txn is a transaction object allowing to perform operations in it.
type Txn interface {
	Get(key string) []byte
	Put(key string, val []byte)
	Delete(key string)
}

var txnKey interface{} = "etcd-txn"

// GetTxn get a txn from context.
func GetTxn(ctx context.Context) Txn {
	iTxn := ctx.Value(txnKey)
	t, _ := iTxn.(Txn) //nolint: errcheck
	return t
}

// WithTxn returns new context with Txn object.
func WithTxn(ctx context.Context, t Txn) context.Context {
	return context.WithValue(ctx, txnKey, t)
}

// StmTxn is an object for software transactional memory
type StmTxn struct {
	conc.STM
	log *logrus.Entry
}

func (s StmTxn) Get(key string) []byte {
	s.log.WithFields(logrus.Fields{"key": key}).Debugf(
		"Getting resource from etcd in transaction")
	return []byte(s.STM.Get(key))
}

func (s StmTxn) Put(key string, val []byte) {
	s.log.WithFields(logrus.Fields{"key": key}).Debugf(
		"Putting resource in etcd in transaction")
	s.STM.Put(key, string(val))
}

func (s StmTxn) Delete(key string) {
	s.log.WithFields(logrus.Fields{"key": key}).Debugf(
		"Deleting resource in etcd in transaction")
	s.STM.Del(key)
}


