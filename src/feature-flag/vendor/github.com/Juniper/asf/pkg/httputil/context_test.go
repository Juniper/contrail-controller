package httputil

import (
	"context"
	"net/http"
	"reflect"
	"testing"
)

const (
	contentTypeHeader    = "Content-Type"
	applicationJSONValue = "application/json"
)

func TestWithHTTPHeader(t *testing.T) {
	tests := []struct {
		name  string
		ctx   context.Context
		key   string
		value string
		want  http.Header
	}{{
		name: "empty strings", want: http.Header{"": []string{""}},
	}, {
		name: "add Content-Type header",
		key:  contentTypeHeader, value: applicationJSONValue,
		want: http.Header{contentTypeHeader: []string{applicationJSONValue}},
	}, {
		name: "add Content-Type header with preexisting value",
		ctx: context.WithValue(
			context.Background(), headersClientContextKey, http.Header{
				contentTypeHeader: []string{"some/value"},
			},
		),
		key: contentTypeHeader, value: applicationJSONValue,
		want: http.Header{contentTypeHeader: []string{applicationJSONValue}},
	}}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if tt.ctx == nil {
				tt.ctx = context.Background()
			}
			got := WithHTTPHeader(tt.ctx, tt.key, tt.value).Value(headersClientContextKey)
			if !reflect.DeepEqual(got, tt.want) {
				t.Errorf(
					"WithHTTPHeader().Value(headersClientContextKey) = %v, want %v", got, tt.want,
				)
			}
		})
	}
}

func TestWithHTTPHeaderHeadersLeak(t *testing.T) {
	ctx := context.Background()
	ctx = WithHTTPHeader(ctx, contentTypeHeader, "old")

	ctxA := WithHTTPHeader(ctx, contentTypeHeader, "A")
	ctxB := WithHTTPHeader(ctx, contentTypeHeader, "B")

	expectedOldValue := http.Header{contentTypeHeader: []string{"old"}}
	if oldValue := ctx.Value(headersClientContextKey); !reflect.DeepEqual(oldValue, expectedOldValue) {
		t.Errorf("header in old context was changed, want: %v, got: %v", expectedOldValue, oldValue)
	}

	expectedAValue := http.Header{contentTypeHeader: []string{"A"}}
	if aValue := ctxA.Value(headersClientContextKey); !reflect.DeepEqual(aValue, expectedAValue) {
		t.Errorf("header in context A was changed, want: %v, got: %v", expectedAValue, aValue)
	}

	expectedBValue := http.Header{contentTypeHeader: []string{"B"}}
	if bValue := ctxB.Value(headersClientContextKey); !reflect.DeepEqual(bValue, expectedBValue) {
		t.Errorf("header in context B was changed, want: %v, got: %v", expectedBValue, bValue)
	}
}

func TestSetContextHeaders(t *testing.T) {
	tests := []struct {
		name       string
		request    *http.Request
		wantHeader http.Header
	}{{
		name: "nil",
	}, {
		name:    "no headers in context",
		request: (&http.Request{}).WithContext(context.Background()),
	}, {
		name: "content-type header set in context",
		request: (&http.Request{}).WithContext(
			context.WithValue(context.Background(), headersClientContextKey, http.Header{
				contentTypeHeader: []string{applicationJSONValue},
			}),
		),
		wantHeader: http.Header{contentTypeHeader: []string{applicationJSONValue}},
	}, {
		name: "context has overwriting headers",
		request: (&http.Request{
			Header: http.Header{contentTypeHeader: []string{"some-value"}},
		}).WithContext(
			context.WithValue(context.Background(), headersClientContextKey, http.Header{
				contentTypeHeader: []string{applicationJSONValue},
			}),
		),
		wantHeader: http.Header{contentTypeHeader: []string{applicationJSONValue}},
	}, {
		name: "context has multiple values for one header",
		request: (&http.Request{}).WithContext(
			context.WithValue(context.Background(), headersClientContextKey, http.Header{
				contentTypeHeader: []string{applicationJSONValue, "irrelevant"},
			}),
		),
		wantHeader: http.Header{contentTypeHeader: []string{applicationJSONValue}},
	}}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			SetContextHeaders(tt.request)
			var headers http.Header
			if tt.request != nil {
				headers = tt.request.Header
			}
			if !reflect.DeepEqual(headers, tt.wantHeader) {
				t.Errorf("(tt.request.Header) %v != (tt.want) %v", headers, tt.wantHeader)
			}
		})
	}
}

func TestCloneHeader(t *testing.T) {
	tests := []struct {
		name string
		h    http.Header
		want http.Header
	}{{
		name: "nils",
	}, {
		name: "empty", h: http.Header{}, want: http.Header{},
	}, {
		name: "one value",
		h:    http.Header{contentTypeHeader: []string{"A"}},
		want: http.Header{contentTypeHeader: []string{"A"}},
	}, {
		name: "more headers",
		h: http.Header{
			"Foo-Header":      []string{"X"},
			contentTypeHeader: []string{"A", "B"},
		},
		want: http.Header{
			"Foo-Header":      []string{"X"},
			contentTypeHeader: []string{"A", "B"},
		},
	}}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if got := cloneHeader(tt.h); !reflect.DeepEqual(got, tt.want) {
				t.Errorf("Clone() = %v, want %v", got, tt.want)
			}
			if !reflect.DeepEqual(tt.h, tt.want) {
				t.Errorf("Clone() mutated original Headers, got %v, want %v", tt.h, tt.want)
			}
		})
	}
}
