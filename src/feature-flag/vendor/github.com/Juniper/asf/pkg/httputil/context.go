package httputil

import (
	"context"
	"net/http"
)

type httputilContextKey string

const (
	headersClientContextKey httputilContextKey = "headers"
)

// WithHTTPHeader creates child context with provided header.
func WithHTTPHeader(ctx context.Context, key, value string) context.Context {
	headers := http.Header{}
	if v, ok := ctx.Value(headersClientContextKey).(http.Header); ok && v != nil {
		headers = cloneHeader(v)
	}
	headers.Set(key, value)
	return context.WithValue(ctx, headersClientContextKey, headers)
}

// cloneHeader returns a copy of h or nil if h is nil.
// TODO(mblotniak): use http.Header.Clone() when Go version is updated to 1.13.
func cloneHeader(h http.Header) http.Header {
	if h == nil {
		return nil
	}

	// Find total number of values.
	nv := 0
	for _, vv := range h {
		nv += len(vv)
	}
	sv := make([]string, nv) // shared backing array for headers' values
	h2 := make(http.Header, len(h))
	for k, vv := range h {
		n := copy(sv, vv)
		h2[k] = sv[:n:n]
		sv = sv[n:]
	}
	return h2
}

// SetContextHeaders sets extra headers that are stored in context.
func SetContextHeaders(r *http.Request) {
	if r == nil {
		return
	}
	if headers, ok := r.Context().Value(headersClientContextKey).(http.Header); ok && len(headers) > 0 {
		if r.Header == nil {
			r.Header = http.Header{}
		}
		for key := range headers {
			r.Header.Set(key, headers.Get(key))
		}
	}
}
