package client

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"reflect"
	"testing"

	"github.com/Juniper/asf/pkg/keystone"
	"github.com/stretchr/testify/assert"
)

func TestNewHTTP(t *testing.T) {
	tests := []struct {
		desc string
		url  string
	}{{
		desc: "http client test",
		url:  "http://fake_endpoint/",
	}, {
		desc: "https client test",
		url:  "https://fake_endpoint/",
	}}
	for _, tt := range tests {
		t.Run(tt.desc, func(t *testing.T) {
			h := NewHTTP(&HTTPConfig{
				ID:       "fake_client",
				Password: "fake_password",
				Endpoint: tt.url,
				AuthURL:  tt.url,
				Insecure: true,
			})

			assert.Equal(t, tt.url+"foo", h.getURL("foo"), "getURL failed")
			assert.Equal(t, tt.url, h.AuthURL, "invalid auth URL")
		})
	}
}

func TestDoRequest(t *testing.T) {
	tests := []struct {
		desc     string
		req      *Request
		resps    []httpResp
		want     testResp
		wantErr  bool
		keystone *keystone.Scope
	}{{
		desc:    "nil request",
		wantErr: true,
	}, {
		desc:  "successful request",
		req:   &Request{Expected: []int{http.StatusOK}},
		resps: []httpResp{{code: http.StatusOK}},
	}, {
		desc:    "no keystone retry request",
		req:     &Request{Expected: []int{http.StatusOK}},
		resps:   []httpResp{{code: http.StatusUnauthorized}, {code: http.StatusOK}},
		wantErr: true,
	}, {
		desc: "successful retry request",
		req:  &Request{Expected: []int{http.StatusOK}},
		resps: []httpResp{
			{code: http.StatusUnauthorized},
			{code: http.StatusOK, body: []byte(`{"StringValue": "this", "IntValue":1000}`)},
		},
		keystone: keystone.NewScope("fake", "", "fake_admin", "fake_admin_project"),
		want:     testResp{StringValue: "this", IntValue: 1000},
	}, {
		desc:     "fail retry",
		req:      &Request{Expected: []int{http.StatusOK}},
		resps:    []httpResp{{code: http.StatusUnauthorized}, {code: http.StatusUnauthorized}, {code: http.StatusOK}},
		wantErr:  true,
		keystone: keystone.NewScope("fake", "", "fake_admin", "fake_admin_project"),
	}}
	for _, tt := range tests {
		t.Run(tt.desc, func(t *testing.T) {
			ts := fakeHTTP(tt.resps)
			defer ts.Close()

			h := NewHTTP(&HTTPConfig{
				ID:       "fake_client",
				Password: "fake_password",
				Endpoint: ts.URL,
				AuthURL:  ts.URL,
				Scope:    tt.keystone,
				Insecure: true,
			})

			var got testResp
			if tt.req != nil {
				tt.req.Output = &got
			}
			_, err := h.DoRequest(context.Background(), tt.req)
			switch {
			case err != nil && tt.wantErr:
				return
			case err != nil && !tt.wantErr:
				t.Fatalf("unexpected error: %v", err)
			case err == nil && tt.wantErr:
				t.Fatalf("unexpected success")
			}
			if !reflect.DeepEqual(got, tt.want) {
				t.Fatalf("DoRequest failed: got %+v, want %+v", got, tt.want)
			}
		})
	}
}

func fakeHTTP(resps []httpResp) *httptest.Server {
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		if req.URL.Path == "/auth/tokens" {
			w.Header().Add("X-Auth-Token", "code")
			w.WriteHeader(http.StatusOK)
			err := json.NewEncoder(w).Encode(keystone.AuthResponse{Token: &keystone.Token{}})
			if err != nil {
				w.WriteHeader(http.StatusInternalServerError)
				return
			}
			return
		}
		if len(resps) == 0 {
			w.WriteHeader(http.StatusInternalServerError)
			return
		}
		resp := resps[0]
		resps = resps[1:]
		if len(resp.body) != 0 {
			if _, err := w.Write(resp.body); err != nil {
				w.WriteHeader(http.StatusInternalServerError)
			}
		}
		w.WriteHeader(resp.code)

	}))
	return ts
}

type httpResp struct {
	code int
	body []byte
}

type testResp struct {
	StringValue string
	IntValue    int
}
