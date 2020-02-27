package errutil

import (
	"net/http"
	"testing"

	"github.com/labstack/echo"
	"github.com/pkg/errors"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

func TestToHTTPError(t *testing.T) {
	tests := []struct {
		name         string
		err          error
		expectedCode int
	}{
		{name: "no error", expectedCode: http.StatusOK},
		{name: "not found error", err: ErrorNotFound, expectedCode: http.StatusNotFound},
		{name: "unauthenticated error", err: ErrorUnauthenticated, expectedCode: http.StatusUnauthorized},
		{name: "permission denied error", err: ErrorPermissionDenied, expectedCode: http.StatusForbidden},
		{
			name:         "permission denied error, wrapped",
			err:          errors.Wrap(ErrorPermissionDenied, "wrapper"),
			expectedCode: http.StatusForbidden,
		},
		{
			name:         "permission denied error, wrapped twice",
			err:          errors.Wrap(errors.Wrap(ErrorPermissionDenied, "wrapper"), "second wrapper"),
			expectedCode: http.StatusForbidden,
		},
		{name: "unknown error", err: errors.New("unknown"), expectedCode: http.StatusInternalServerError},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			result := ToHTTPError(tt.err)
			httpErr, ok := result.(*echo.HTTPError)
			require.True(t, ok)
			assert.Equal(t, tt.expectedCode, httpErr.Code)
		})
	}
}
