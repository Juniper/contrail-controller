package integration

import (
	"net/http/httptest"
	"testing"

	"github.com/Juniper/asf/pkg/keystone"
	"github.com/labstack/echo"
	"github.com/pkg/errors"
	"github.com/stretchr/testify/require"

	kstypes "github.com/Juniper/asf/pkg/keystone"
)

// NewKeystoneServerFake creates started Keystone server fake.
// It registers given user in default domain with admin role.
func NewKeystoneServerFake(t *testing.T, keystoneAuthURL, user, password string) *httptest.Server {
	e := echo.New()

	// TODO(dfurman): do not register local Keystone endpoints (/keystone/v3/...) for Keystone fake
	k, err := keystone.Init(e, nil)
	if err != nil {
		return nil
	}
	if user != "" {
		k, err = withKeystoneUser(k, user, password)
		require.NoError(t, err)
	}

	e = withRegisteredRoutes(e, k)
	return httptest.NewServer(e)
}

func withKeystoneUser(k *keystone.Keystone, user, password string) (*keystone.Keystone, error) {
	sa, ok := k.Assignment.(*keystone.StaticAssignment)
	if !ok {
		return nil, errors.New("failed to add user to Keystone fake: wrong Assignment type")
	}

	sa.Users = map[string]*kstypes.User{}
	sa.Users[user] = &kstypes.User{
		Domain:   sa.Domains[DefaultDomainID],
		ID:       user,
		Name:     user,
		Password: password,
		Roles: []*kstypes.Role{
			{
				ID:      AdminRoleID,
				Name:    AdminRoleName,
				Project: sa.Projects[AdminProjectID],
			},
		},
	}
	return k, nil
}

func withRegisteredRoutes(e *echo.Echo, k *keystone.Keystone) *echo.Echo {
	e.POST("/v3/auth/tokens", k.CreateTokenAPI)
	e.GET("/v3/auth/tokens", k.ValidateTokenAPI)

	// TODO: Remove this, since "/keystone/v3/projects" is a keystone endpoint
	e.GET("/v3/auth/projects", k.ListProjectsAPI)

	e.GET("/v3/projects", k.ListProjectsAPI)
	e.GET("/v3/project/:id", k.GetProjectAPI)

	return e
}
