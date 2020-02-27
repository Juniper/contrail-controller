package replication

import (
	"context"
	"io"

	"github.com/jackc/pgx"
	"github.com/lib/pq"
	"github.com/pkg/errors"
)

const pgQueryCanceledErrorCode = "57014"

func isContextCancellationError(err error) bool {
	if pqErr, ok := errors.Cause(err).(*pq.Error); ok {
		if pqErr.Code == pgQueryCanceledErrorCode {
			return true
		}
	}
	if errors.Cause(err) == context.Canceled {
		return true
	}
	return false
}

type causeError struct {
	error
}

func (e causeError) Cause() error {
	return errors.Cause(e.error)
}

type syncError struct {
	causeError
}

func wrapError(err error) error {
	if err == nil {
		return nil
	}
	return syncError{causeError: causeError{error: err}}
}

func (e syncError) Temporary() bool {
	c := errors.Cause(e.error)
	if c == io.EOF || c == pgx.ErrConnBusy {
		return true
	}
	return false
}

type temporaryError struct {
	causeError
}

func markTemporaryError(err error) error {
	if err == nil {
		return nil
	}
	return temporaryError{causeError: causeError{error: err}}
}

func (e temporaryError) Temporary() bool {
	return true
}
