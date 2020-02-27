package keystone

import (
	"bytes"
	"context"
	"io/ioutil"
	"net/http"
	"reflect"
	"testing"
)

func Test_encodeQueryParameters(t *testing.T) {
	tests := []struct {
		name string
		qs   []QueryParameter
		want string
	}{
		{name: "nil", want: ""},
		{name: "empty", qs: []QueryParameter{}, want: ""},
		{name: "two values", qs: []QueryParameter{{"a", "v"}, {"b", "n"}}, want: "?a=v&b=n"},
		{name: "duplicated", qs: []QueryParameter{{"a", "v"}, {"a", "n"}}, want: "?a=v&a=n"},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if got := encodeQueryParameters(tt.qs); got != tt.want {
				t.Errorf("encodeQueryParameters() = %v, want %v", got, tt.want)
			}
		})
	}
}

func TestKeystoneProjectListing(t *testing.T) {
	tests := []struct {
		name     string
		HTTPDoer doer
		want     Projects
		wantErr  bool
	}{{
		name: "keystone returns sample projects",
		HTTPDoer: &mockDoer{
			StatusCode: http.StatusOK,
			Body:       []byte(`{"projects":[{"id":"ff4e51"},{"id":"asd"}]}`),
		},
		want: Projects{{ID: "ff4e51"}, {ID: "asd"}},
	}, {
		name:     "keystone returns invalid fields",
		HTTPDoer: &mockDoer{StatusCode: http.StatusOK, Body: []byte(`{"foo":{"bar":"foobar"}}`)},
	}, {
		name:     "keystone returns bad response",
		HTTPDoer: &mockDoer{StatusCode: http.StatusOK, Body: []byte(`{"foo":"bar":"foobar"}}`)},
		wantErr:  true,
	}, {
		name:     "got empty response from keystone",
		HTTPDoer: &mockDoer{StatusCode: http.StatusOK, Body: []byte{}},
		wantErr:  true,
	}, {
		name:     "keystone returns StatusForbidden",
		HTTPDoer: &mockDoer{StatusCode: http.StatusForbidden},
		wantErr:  true,
	}}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			k := &Client{HTTPDoer: tt.HTTPDoer}
			listProjects := func(ctx context.Context) (Projects, error) { return k.ListProjects(ctx) }
			for methodName, method := range map[string](func(context.Context) (Projects, error)){
				"ListProjects":               listProjects,
				"ListAvailableProjectScopes": k.ListAvailableProjectScopes,
			} {
				t.Run(methodName, func(t *testing.T) {
					got, err := method(context.Background())
					if (err != nil) != tt.wantErr {
						t.Errorf("Keystone.%s() error = %v, wantErr %v", methodName, err, tt.wantErr)
						t.FailNow()
					}
					if !reflect.DeepEqual(got, tt.want) {
						t.Errorf("Keystone.%s() = %v, want %v", methodName, got, tt.want)
					}
				})
			}
		})
	}
}

func TestKeystone_CreateUser(t *testing.T) {
	tests := []struct {
		name     string
		HTTPDoer doer
		want     User
		wantErr  bool
	}{{
		name:     "keystone returns sample user",
		HTTPDoer: &mockDoer{StatusCode: http.StatusCreated, Body: []byte(`{"user":{"id":"ff4e51"}}`)},
		want:     User{ID: "ff4e51"},
	}, {
		name:     "keystone returns invalid fields",
		HTTPDoer: &mockDoer{StatusCode: http.StatusCreated, Body: []byte(`{"foo":{"bar":"foobar"}}`)},
	}, {
		name:     "keystone returns bad response",
		HTTPDoer: &mockDoer{StatusCode: http.StatusCreated, Body: []byte(`{"foo":"bar":"foobar"}}`)},
		wantErr:  true,
	}, {
		name:     "got empty response from keystone",
		HTTPDoer: &mockDoer{StatusCode: http.StatusCreated, Body: []byte{}},
		wantErr:  true,
	}, {
		name:     "keystone returns StatusForbidden",
		HTTPDoer: &mockDoer{StatusCode: http.StatusForbidden},
		wantErr:  true,
	}}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			k := &Client{
				HTTPDoer: tt.HTTPDoer,
			}
			got, err := k.CreateUser(context.Background(), User{})
			if (err != nil) != tt.wantErr {
				t.Errorf("Keystone.CreateUser() error = %v, wantErr %v", err, tt.wantErr)
				return
			}
			if !reflect.DeepEqual(got, tt.want) {
				t.Errorf("Keystone.CreateUser() = %v, want %v", got, tt.want)
			}
		})
	}
}

func TestKeystone_AssignProjectRoleOnUser(t *testing.T) {
	tests := []struct {
		HTTPDoer doer
		name     string
		wantErr  bool
	}{{
		name:     "keystone returns StatusBadRequest",
		HTTPDoer: &mockDoer{StatusCode: http.StatusBadRequest},
		wantErr:  true,
	}, {
		name:     "keystone returns StatusForbidden",
		HTTPDoer: &mockDoer{StatusCode: http.StatusForbidden},
		wantErr:  true,
	}, {
		name:     "keystone returns StatusNoContent",
		HTTPDoer: &mockDoer{StatusCode: http.StatusNoContent},
	}}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			k := &Client{
				HTTPDoer: tt.HTTPDoer,
			}
			if err := k.AssignProjectRoleOnUser(
				context.Background(),
				User{ID: ""},
				Role{ID: "", Project: &Project{ID: ""}},
			); (err != nil) != tt.wantErr {
				t.Errorf("Keystone.AssignProjectRoleOnUser() error = %v, wantErr %v", err, tt.wantErr)
			}
		})
	}
}

func TestKeystone_checkServiceUserExists(t *testing.T) {
	tests := []struct {
		name     string
		HTTPDoer doer
		want     bool
		wantErr  bool
	}{{
		name:     "service user not found",
		HTTPDoer: &mockDoer{StatusCode: http.StatusOK, Body: []byte(`{"users": []}`)},
	}, {
		name:     "keystone returns StatusForbidden",
		HTTPDoer: &mockDoer{StatusCode: http.StatusForbidden},
		wantErr:  true,
	}, {
		name:     "keystone returns service user",
		HTTPDoer: &mockDoer{StatusCode: http.StatusOK, Body: []byte(`{"users":[{"name":"goapi", "password" : "goapi123", "roles" : [{"Name": "admin", "project": {"name": "service"}}]}]}`)},
		want:     true,
	}, {
		name:     "keystone returns service user without roles",
		HTTPDoer: &mockDoer{StatusCode: http.StatusOK, Body: []byte(`{"users":[{"name":"goapi", "password" : "goapi123"}]}`)},
	}}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			k := &Client{
				HTTPDoer: tt.HTTPDoer,
			}
			got, err := k.checkServiceUserExists(context.Background(), User{Name: "goapi", Password: "goapi123"})
			if (err != nil) != tt.wantErr {
				t.Errorf("Keystone.checkServiceUserExists() error = %v, wantErr %v", err, tt.wantErr)
				return
			}
			if got != tt.want {
				t.Errorf("Keystone.checkServiceUserExists() = %v, want %v", got, tt.want)
			}
		})
	}
}

type mockDoer struct {
	StatusCode int
	Body       []byte
}

func (m *mockDoer) Do(req *http.Request) (*http.Response, error) {
	return newResponse(m.StatusCode, m.Body), nil
}

func newResponse(statusCode int, body []byte) *http.Response {
	return &http.Response{StatusCode: statusCode, Body: ioutil.NopCloser(bytes.NewReader(body))}
}
