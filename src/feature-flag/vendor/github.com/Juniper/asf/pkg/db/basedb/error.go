package basedb

import (
	"database/sql"

	"github.com/lib/pq"
	"github.com/sirupsen/logrus"

	"github.com/Juniper/asf/pkg/errutil"
)

const (
	pgUniqueViolation     = "unique_violation"
	pgForeignKeyViolation = "foreign_key_violation"
)

//FormatDBError converts DB specific error.
func FormatDBError(err error) error {
	if err == nil {
		return nil
	}

	if publicErr := getPublicError(err); publicErr != nil {
		logrus.Debugf("Database error: %v. Returning: %v", err, publicErr)
		return publicErr
	}
	logrus.Error("Unknown database error:", err)
	return err
}

// getPublicError returns an error with an API error code and a high-level error message.
// If err is not recognized, nil is returned.
func getPublicError(err error) error {
	if err == sql.ErrNoRows {
		return errutil.ErrorNotFound
	}

	switch err.(type) {
	case *pq.Error:
		return getPublicPGError(err.(*pq.Error))
	default:
		return nil
	}
}

func getPublicPGError(err *pq.Error) error {
	switch err.Code.Name() {
	case pgUniqueViolation:
		return uniqueConstraintViolation()
	case pgForeignKeyViolation:
		return foreignKeyConstraintViolation()
	default:
		return nil
	}
}

func uniqueConstraintViolation() error {
	return errutil.ErrorConflictf("Resource conflict: unique constraint violation")
}

func foreignKeyConstraintViolation() error {
	return errutil.ErrorConflictf("Resource conflict: foreign key constraint violation")
}
