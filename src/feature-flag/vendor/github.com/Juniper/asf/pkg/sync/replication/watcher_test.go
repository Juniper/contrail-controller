package replication

import (
	"context"
	"encoding/binary"
	"testing"
	"time"

	"github.com/Juniper/asf/pkg/logutil"
	"github.com/jackc/pgx"
	"github.com/kyleconroy/pgoutput"
	"github.com/pkg/errors"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/mock"
)

type oner interface {
	On(string, ...interface{}) *mock.Call
}

func TestPostgresWatcherWatch(t *testing.T) {
	const slot, publication, snapshot, lsn = "test-sub", "test-pub", "snapshot-id", uint64(2778)
	cancel := func() {}

	tests := []struct {
		name            string
		initMock        func(oner)
		expectedMessage pgoutput.Message
		watchError      bool
	}{
		{
			name: "should return error when GetReplicationSlot fails",
			initMock: func(o oner) {
				o.On("IsInRecovery", mock.Anything).Return(false, nil).Once()
				o.On("GetReplicationSlot", mock.Anything).Return(uint64(0), "", assert.AnError).Once()
			},
			watchError: true,
		},
		{
			name: "should return error when DoInTransactionSnapshot fails",
			initMock: func(o oner) {
				o.On("IsInRecovery", mock.Anything).Return(false, nil).Once()
				o.On("GetReplicationSlot", mock.Anything).Return(lsn, snapshot, nil).Once()
				o.On("DoInTransactionSnapshot", mock.Anything, snapshot, mock.Anything).Return(assert.AnError).Once()
			},
			watchError: true,
		},
		{
			name: "should return error when StartReplication fails",
			initMock: func(o oner) {
				o.On("IsInRecovery", mock.Anything).Return(false, nil).Once()
				o.On("GetReplicationSlot", mock.Anything).Return(lsn, snapshot, nil).Once()
				o.On("DoInTransactionSnapshot", mock.Anything, snapshot, mock.Anything).Return(nil).Once()
				o.On("StartReplication", slot, publication, uint64(0)).Return(assert.AnError).Once()
			},
			watchError: true,
		},
		{
			name: "should return error when WaitForReplicationMessage returns unknown error",
			initMock: func(o oner) {
				o.On("IsInRecovery", mock.Anything).Return(false, nil).Once()
				o.On("GetReplicationSlot", mock.Anything).Return(lsn, snapshot, nil).Once()
				o.On("DoInTransactionSnapshot", mock.Anything, snapshot, mock.Anything).Return(nil).Once()
				o.On("StartReplication", slot, publication, uint64(0)).Return(nil).Once()
				o.On("WaitForReplicationMessage", mock.Anything).Return(nil, assert.AnError).Once()
			},
			watchError: true,
		},
		{
			name: "should stop on WaitForReplicationMessage when context cancelled",
			initMock: func(o oner) {
				o.On("IsInRecovery", mock.Anything).Return(false, nil).Once()
				o.On("GetReplicationSlot", mock.Anything).Return(lsn, snapshot, nil).Once()
				o.On("DoInTransactionSnapshot", mock.Anything, snapshot, mock.Anything).Return(nil).Once()
				o.On("StartReplication", slot, publication, uint64(0)).Return(nil).Once()
				o.On("WaitForReplicationMessage", mock.Anything).Run(func(mock.Arguments) {
					cancel()
				}).Return((*pgx.ReplicationMessage)(nil), nil).Once()
				o.On("Close").Return(nil).Once()
			},
		},
		{
			name: "should continue when WaitForReplicationMessage returns context deadline",
			initMock: func(o oner) {
				o.On("IsInRecovery", mock.Anything).Return(false, nil).Once()
				o.On("GetReplicationSlot", mock.Anything).Return(lsn, snapshot, nil).Once()
				o.On("DoInTransactionSnapshot", mock.Anything, snapshot, mock.Anything).Return(nil).Once()
				o.On("StartReplication", slot, publication, uint64(0)).Return(nil).Once()
				o.On("WaitForReplicationMessage", mock.Anything).Return(nil, context.DeadlineExceeded).Twice()
				o.On("WaitForReplicationMessage", mock.Anything).Run(func(mock.Arguments) {
					cancel()
				}).Return((*pgx.ReplicationMessage)(nil), nil).Once()
				o.On("Close").Return(nil).Once()
			},
			watchError: false,
		},
		{
			name: "should pass to handler received WAL message",
			initMock: func(o oner) {
				o.On("IsInRecovery", mock.Anything).Return(false, nil).Once()
				o.On("GetReplicationSlot", mock.Anything).Return(lsn, snapshot, nil).Once()
				o.On("DoInTransactionSnapshot", mock.Anything, snapshot, mock.Anything).Return(nil).Once()
				o.On("StartReplication", slot, publication, uint64(0)).Return(nil).Once()
				o.On("WaitForReplicationMessage", mock.Anything).Return(
					&pgx.ReplicationMessage{WalMessage: &pgx.WalMessage{WalData: getBeginData(pgoutput.Begin{})}},
					nil,
				).Twice()
				o.On("WaitForReplicationMessage", mock.Anything).Run(func(mock.Arguments) {
					cancel()
				}).Return((*pgx.ReplicationMessage)(nil), nil).Once()
				o.On("Close").Return(nil).Once()
			},
			expectedMessage: pgoutput.Begin{
				Timestamp: time.Date(2000, time.January, 1, 0, 0, 0, 0, time.UTC),
			},
			watchError: false,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			var receivedMsg pgoutput.Message
			pwc := &mockPostgresWatcherConnection{}
			if tt.initMock != nil {
				tt.initMock(pwc)
			}
			w := givenPostgresWatcher(
				slot,
				publication,
				pwc,
				func(_ context.Context, m pgoutput.Message) error {
					receivedMsg = m
					return nil
				},
			)

			var ctx context.Context
			ctx, cancel = context.WithCancel(context.Background())

			err := w.Watch(ctx)

			if tt.watchError {
				assert.Error(t, err)
			} else {
				assert.NoError(t, err)
			}
			assert.Equal(t, tt.expectedMessage, receivedMsg)
			pwc.AssertExpectations(t)
			cancel()
		})
	}
}

func TestPostgresWatcherContextCancellation(t *testing.T) {
	// given
	slot, publication, snapshot, lsn := "test-sub", "test-pub", "snapshot-id", uint64(2778)

	closeErr := errors.New("some closing error")

	pwc := &mockPostgresWatcherConnection{}
	pwc.On("IsInRecovery", mock.Anything).Return(false, nil).Once()
	pwc.On("GetReplicationSlot", mock.Anything).Return(lsn, snapshot, nil).Once()
	pwc.On("DoInTransactionSnapshot", mock.Anything, snapshot, mock.Anything).Return(nil).Once()
	pwc.On("StartReplication", slot, publication, uint64(0)).Return(nil).Once()
	pwc.On("Close").Return(closeErr).Once()

	w := givenPostgresWatcher(slot, publication, pwc, nil)

	ctx, cancel := context.WithCancel(context.Background())

	// when
	cancel()
	err := w.Watch(ctx)

	// then
	assert.Equal(t, closeErr, errors.Cause(err))
	pwc.AssertExpectations(t)
}

func TestPostgresWatcherClose(t *testing.T) {
	canceled := false
	cancel := func() {
		canceled = true
	}
	w := givenPostgresWatcher("", "", &mockPostgresWatcherConnection{}, nil)
	w.cancel = cancel

	w.Close()

	assert.True(t, canceled)
}

func getBeginData(m pgoutput.Begin) []byte {
	b := make([]byte, 21)
	b[0] = 'B'
	binary.BigEndian.PutUint64(b[1:], m.LSN)
	binary.BigEndian.PutUint64(b[9:], 0)
	binary.BigEndian.PutUint32(b[17:], uint32(m.XID))
	return b
}

func givenPostgresWatcher(
	slot, publication string,
	conn postgresWatcherConnection,
	handler Handler,
) *PostgresWatcher {
	return &PostgresWatcher{
		conf: PostgresSubscriptionConfig{
			StatusTimeout: time.Second,
			Slot:          slot,
			Publication:   publication,
		},
		conn:       conn,
		handler:    handler,
		log:        logutil.NewLogger("postgres-watcher"),
		shouldDump: true,
		dumpDoneCh: make(chan struct{}),
	}
}

type mockPostgresWatcherConnection struct {
	mock.Mock
}

func (pwc *mockPostgresWatcherConnection) Close() error {
	args := pwc.MethodCalled("Close")
	return args.Error(0)
}

func (pwc *mockPostgresWatcherConnection) GetReplicationSlot(
	name string,
) (lastLSN uint64, snapshotName string, err error) {
	args := pwc.MethodCalled("GetReplicationSlot", name)
	return args.Get(0).(uint64), args.String(1), args.Error(2)
}

func (pwc *mockPostgresWatcherConnection) RenewPublication(ctx context.Context, name string) error {
	args := pwc.MethodCalled("RenewPublication", ctx, name)
	return args.Error(0)
}

func (pwc *mockPostgresWatcherConnection) StartReplication(slot string, publication string, startLSN uint64) error {
	args := pwc.MethodCalled("StartReplication", slot, publication, startLSN)
	return args.Error(0)
}

func (pwc *mockPostgresWatcherConnection) WaitForReplicationMessage(
	ctx context.Context,
) (*pgx.ReplicationMessage, error) {
	args := pwc.MethodCalled("WaitForReplicationMessage", ctx)
	if args.Error(1) != nil {
		return nil, args.Error(1)
	}
	return args.Get(0).(*pgx.ReplicationMessage), args.Error(1)
}

func (pwc *mockPostgresWatcherConnection) SendStatus(receivedLSN, savedLSN uint64) error {
	args := pwc.MethodCalled("SendStatus", receivedLSN, savedLSN)
	return args.Error(0)
}
func (pwc *mockPostgresWatcherConnection) IsInRecovery(ctx context.Context) (bool, error) {
	args := pwc.MethodCalled("IsInRecovery", ctx)
	return args.Get(0).(bool), args.Error(1)
}

func (pwc *mockPostgresWatcherConnection) DoInTransactionSnapshot(
	ctx context.Context, snapshotName string, do func(context.Context) error,
) error {
	args := pwc.MethodCalled("DoInTransactionSnapshot", ctx, snapshotName, do)
	return args.Error(0)
}
