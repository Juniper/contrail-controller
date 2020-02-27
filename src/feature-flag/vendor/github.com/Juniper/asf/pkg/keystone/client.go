package keystone

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"path"

	"github.com/pkg/errors"

	"github.com/Juniper/asf/pkg/httputil"
)

const (
	// AdminRoleName is default role name for keystone admin.
	AdminRoleName = "admin"

	// xAuthTokenHeader is a header used by keystone to store user auth tokens.
	xAuthTokenHeader = "X-Auth-Token"
	// xSubjectTokenHeader is a header used by keystone to return new tokens.
	xSubjectTokenHeader = "X-Subject-Token"

	contentTypeHeader    = "Content-Type"
	applicationJSONValue = "application/json"
	serviceProjectName   = "service"
)

type serviceUserNotFound struct {
	user string
}

func (e *serviceUserNotFound) Error() string {
	return fmt.Sprintf("user '%s' does not exist", e.user)
}

// WithXAuthToken creates child context with Auth Token
func WithXAuthToken(ctx context.Context, token string) context.Context {
	return httputil.WithHTTPHeader(ctx, xAuthTokenHeader, token)
}

type doer interface {
	Do(req *http.Request) (*http.Response, error)
}

// Client is a keystone client.
type Client struct {
	URL      string
	HTTPDoer doer
}

type projectResponse struct {
	Project Project `json:"project"`
}

type projectListResponse struct {
	Projects []*Project `json:"projects"`
}

// GetProject gets project.
func (k *Client) GetProject(ctx context.Context, token string, id string) (*Project, error) {
	ctx = WithXAuthToken(ctx, token)

	var response projectResponse
	if _, err := k.do(
		ctx, http.MethodGet, "/projects/"+id, []int{http.StatusOK}, nil, &response,
	); err != nil {
		return nil, err
	}
	return &response.Project, nil
}

func (k *Client) do(
	ctx context.Context, method, requestPath string, expectedCodes []int, input, output interface{},
) (*http.Response, error) {
	var payload io.Reader
	if input != nil {
		b, err := json.Marshal(input)
		if err != nil {
			return nil, errors.Wrap(err, "marshalling keystone request")
		}
		payload = bytes.NewReader(b)
	}
	request, err := http.NewRequest(method, k.URL+requestPath, payload)
	if err != nil {
		return nil, errors.Wrap(err, "creating HTTP request failed")
	}
	request = request.WithContext(ctx) // TODO(mblotniak): use http.NewRequestWithContext after go 1.13 upgrade
	httputil.SetContextHeaders(request)

	resp, err := k.HTTPDoer.Do(request)
	if err != nil {
		return nil, errors.Wrap(err, "issuing HTTP request failed")
	}
	defer resp.Body.Close() // nolint: errcheck

	if err := httputil.CheckStatusCode(expectedCodes, resp.StatusCode); err != nil {
		return nil, httputil.ErrorFromResponse(err, resp)
	}

	if output != nil {
		if err := json.NewDecoder(resp.Body).Decode(output); err != nil {
			return nil, errors.Wrapf(httputil.ErrorFromResponse(err, resp), "decoding response body failed")
		}
	}

	return resp, nil
}

// QueryParameter is a struct describing a single http query parameter.
type QueryParameter struct {
	Key   string
	Value string
}

func encodeQueryParameters(qs []QueryParameter) string {
	if len(qs) == 0 {
		return ""
	}
	v := url.Values{}
	for _, q := range qs {
		v.Add(q.Key, q.Value)
	}
	return "?" + v.Encode()
}

// ListProjects lists all projects.
// Note that this method requires an scoped token.
func (k *Client) ListProjects(ctx context.Context, params ...QueryParameter) (Projects, error) {
	var response projectListResponse
	_, err := k.do(
		ctx, http.MethodGet, "/projects"+encodeQueryParameters(params), []int{http.StatusOK}, nil, &response,
	)
	return response.Projects, err
}

// GetProjectIDByName finds project id using project name.
func (k *Client) GetProjectIDByName(ctx context.Context, projectName string) (string, error) {
	projects, err := k.ListProjects(ctx, QueryParameter{Key: "name", Value: projectName})
	if err != nil {
		return "", err
	}

	p := projects.FindByName(projectName)
	if p == nil {
		return "", errors.Errorf("could not find project with name %q", projectName)
	}
	return p.ID, nil
}

// ListAvailableProjectScopes lists projects that are available to be scoped to based on
// the unscoped token provided in context.
func (k *Client) ListAvailableProjectScopes(ctx context.Context) (Projects, error) {
	var response projectListResponse
	_, err := k.do(
		ctx, http.MethodGet, "/auth/projects", []int{http.StatusOK}, nil, &response,
	)
	return response.Projects, err
}

// ObtainUnscopedToken gets unscoped authentication token.
func (k *Client) ObtainUnscopedToken(
	ctx context.Context, id, password string, domain *Domain,
) (string, error) {
	if k.URL == "" {
		return "", nil
	}
	return k.fetchToken(ctx, &UnScopedAuthRequest{
		Auth: &UnScopedAuth{
			Identity: &Identity{
				Methods: []string{"password"},
				Password: &Password{
					User: &User{
						Name:     id,
						Password: password,
						Domain:   domain,
					},
				},
			},
		},
	})
}

// fetchToken gets scoped/unscoped token.
func (k *Client) fetchToken(ctx context.Context, authRequest interface{}) (string, error) {
	ctx = httputil.WithHTTPHeader(ctx, contentTypeHeader, applicationJSONValue)
	resp, err := k.do(
		ctx, http.MethodPost, "/auth/tokens", []int{http.StatusOK, http.StatusCreated}, authRequest, nil,
	)
	if err != nil {
		return "", err
	}
	return resp.Header.Get(xSubjectTokenHeader), nil
}

// ObtainToken gets authentication token.
func (k *Client) ObtainToken(ctx context.Context, id, password string, scope *Scope) (string, error) {
	if k.URL == "" {
		return "", nil
	}
	return k.fetchToken(ctx, &ScopedAuthRequest{
		Auth: &ScopedAuth{
			Identity: &Identity{
				Methods: []string{"password"},
				Password: &Password{
					User: &User{
						Name:     id,
						Password: password,
						Domain:   scope.GetDomain(),
					},
				},
			},
			Scope: scope,
		},
	})
}

// CreateUser creates user in keystone.
func (k *Client) CreateUser(ctx context.Context, user User) (User, error) {
	var response CreateUserResponse
	httpResp, err := k.do(
		ctx,
		http.MethodPost,
		"/users",
		[]int{http.StatusCreated, http.StatusConflict},
		CreateUserRequest{User: user},
		&response,
	)
	if err != nil {
		return User{}, err
	}
	if httpResp.StatusCode == http.StatusConflict {
		return k.GetUserByName(ctx, user.Name)
	}
	return response.User, nil
}

type userListResponse struct {
	Users []User `json:"users" yaml:"users"`
}

// GetUserByName looks for role by name in keystone.
func (k *Client) GetUserByName(ctx context.Context, userName string) (User, error) {
	var users userListResponse
	if _, err := k.do(
		ctx, http.MethodGet, fmt.Sprintf("/users?name=%v", userName), []int{http.StatusOK}, nil, &users,
	); err != nil {
		return User{}, err
	}
	for _, user := range users.Users {
		if user.Name == userName {
			return user, nil
		}
	}
	return User{}, &serviceUserNotFound{userName}
}

// createServiceUser creates service user in keystone.
func (k *Client) createServiceUser(ctx context.Context, user User) (User, error) {
	projectID, err := k.GetProjectIDByName(ctx, serviceProjectName)
	if err != nil {
		return User{}, err
	}

	user, err = k.CreateUser(ctx, user)
	if err != nil {
		return User{}, err
	}
	role, err := k.GetRoleByName(ctx, AdminRoleName)
	if err != nil {
		return User{}, err
	}
	role.Project = &Project{ID: projectID}
	if err := k.AssignProjectRoleOnUser(ctx, user, role); err != nil {
		return User{}, err
	}

	return user, nil
}

// checkServiceUserExists checks for service user in keystone.
func (k *Client) checkServiceUserExists(ctx context.Context, user User) (bool, error) {
	servUser, err := k.GetUserByName(ctx, user.Name)
	if err != nil {
		switch err.(type) {
		case *serviceUserNotFound:
			return false, nil
		default:
			return false, err
		}
	}
	if servUser.Password == user.Password {
		for _, role := range servUser.Roles {
			if role.Name == AdminRoleName && role.Project.Name == serviceProjectName {
				return true, nil
			}
		}
	}
	return false, nil
}

// EnsureServiceUserCreated ensures that service user is present in keystone.
// This is done to avoid calling "/roles" endpoint when service user is present.
func (k *Client) EnsureServiceUserCreated(ctx context.Context, user User) (User, error) {
	userPresent, err := k.checkServiceUserExists(ctx, user)
	if err != nil {
		return User{}, err
	}
	if userPresent {
		return user, nil
	}

	serviceUser, err := k.createServiceUser(ctx, user)
	if err != nil {
		return User{}, err
	}
	return serviceUser, nil
}

// AssignProjectRoleOnUser adds role to user in keystone.
func (k *Client) AssignProjectRoleOnUser(ctx context.Context, user User, role Role) error {
	_, err := k.do(
		ctx,
		http.MethodPut,
		path.Join("/projects", role.Project.ID, "users", user.ID, "roles", role.ID),
		[]int{http.StatusNoContent},
		nil,
		nil,
	)
	return err
}

type rolesListResponse struct {
	Roles []Role `json:"roles" yaml:"roles"`
}

// GetRoleByName looks for role by name in keystone.
func (k *Client) GetRoleByName(ctx context.Context, roleName string) (Role, error) {
	var roles rolesListResponse
	if _, err := k.do(
		ctx, http.MethodGet, fmt.Sprintf("/roles?name=%v", roleName), []int{http.StatusOK}, nil, &roles,
	); err != nil {
		return Role{}, err
	}

	for _, role := range roles.Roles {
		if role.Name == roleName {
			return role, nil
		}
	}
	return Role{}, errors.Errorf("role '%s' does not exist", roleName)
}
