package httputil

import (
	"net/http"
	"net/http/httputil"

	"github.com/pkg/errors"
)

func CheckStatusCode(expected []int, actual int) error {
	for _, e := range expected {
		if e == actual {
			return nil
		}
	}
	return errors.Errorf("unexpected return code: expected %v, actual %v", expected, actual)
}

func ErrorFromResponse(e error, r *http.Response) error {
	if r == nil {
		return errors.Wrap(e, "HTTP response is nil, error")
	}
	b, err := httputil.DumpResponse(r, true)
	if err != nil {
		return errors.Wrapf(e, "HTTP response: failed to dump (%s)", err)
	}
	return errors.Wrapf(e, "HTTP response:\n%v", string(b))
}
