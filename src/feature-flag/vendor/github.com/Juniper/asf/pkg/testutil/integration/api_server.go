package integration

import (
	"net/http/httptest"
	"path"
	"testing"

	"github.com/Juniper/asf/pkg/keystone"
	"github.com/Juniper/asf/pkg/logutil"
	"github.com/Juniper/asf/pkg/testutil"
	"github.com/pkg/errors"
	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	kstypes "github.com/Juniper/asf/pkg/keystone"
	integrationetcd "github.com/Juniper/asf/pkg/testutil/integration/etcd"
	//TODO(mlastawiecki): uncomment once the file compiles
	//                    commented due to go mod errors
	//"github.com/Juniper/asf/pkg/apisrv"
	//"github.com/Juniper/asf/pkg/constants"
	//"github.com/Juniper/asf/pkg/cache"
)

const (
	dbUser     = "root"
	dbPassword = "contrail123"
	dbName     = "contrail_test"
)

// Keystone credentials.
const (
	DefaultDomainID     = "default"
	DefaultDomainName   = "default"
	AdminProjectID      = "admin"
	AdminProjectName    = "admin"
	DemoProjectID       = "demo"
	DemoProjectName     = "demo"
	NeutronProjectID    = "aa907485e1f94a14834d8c69ed9cb3b2"
	NeutronProjectName  = "neutron"
	ServiceProjectID    = "service-uuid"
	ServiceProjectName  = "service"
	AdminRoleID         = "admin"
	AdminRoleName       = "admin"
	NeutronRoleID       = "aa907485e1f94a14834d8c69ed9cb3b2"
	NeutronRoleName     = "neutron"
	MemberRoleID        = "Member"
	MemberRoleName      = "Member"
	AdminUserID         = "alice"
	AdminUserName       = "Alice"
	AdminUserPassword   = "alice_password"
	KSAdminUserID       = "admin"
	KSAdminUserName     = "admin"
	KSAdminUserPassword = "contrail123"
	BobUserID           = "bob"
	BobUserName         = "Bob"
	BobUserPassword     = "bob_password"
	ServiceUserID       = "goapi-uuid"
	ServiceUserName     = "goapi"
	ServiceUserPassword = "goapi"
)

// APIServer is embedded API Server for testing purposes.
type APIServer struct {
	APIServer  *apisrv.Server
	testServer *httptest.Server
	log        *logrus.Entry
}

// APIServerConfig contains parameters for test API Server.
type APIServerConfig struct {
	CacheDB            *cache.DB
	RepoRootPath       string
	LogLevel           string
	EnableEtcdNotifier bool
	DisableLogAPI      bool
	EnableRBAC         bool
}

// NewRunningAPIServer creates new running test API Server for testing purposes.
// Call Close() method to release its resources.
func NewRunningAPIServer(t *testing.T, c *APIServerConfig) *APIServer {
	s, err := NewRunningServer(c)
	require.NoError(t, err)

	return s
}

// NewRunningServer creates new running API server with default testing configuration.
// Call Close() method to release its resources.
func NewRunningServer(c *APIServerConfig) (*APIServer, error) {
	setViperConfig(c)

	if err := logutil.Configure(c.LogLevel); err != nil {
		return nil, err
	}

	s, err := apisrv.NewServer()
	if err != nil {
		return nil, errors.Wrapf(err, "creating API Server failed")
	}
	s.Cache = c.CacheDB

	ts := testutil.NewTestHTTPServer(s.Echo)
	viper.Set("keystone.authurl", ts.URL+keystone.LocalAuthPath)
	viper.Set("client.endpoint", ts.URL)

	if err = s.Init(); err != nil {
		return nil, errors.Wrapf(err, "initialization of test API Server failed")
	}

	return &APIServer{
		APIServer:  s,
		testServer: ts,
		log:        logutil.NewLogger("api-server"),
	}, nil
}

func setViperConfig(c *APIServerConfig) {
	setViper(map[string]interface{}{
		"aaa_mode":                    rbacConfig(c.EnableRBAC),
		"database.host":               "localhost",
		"database.user":               dbUser,
		"database.name":               dbName,
		"database.password":           dbPassword,
		"database.max_open_conn":      100,
		"database.connection_retries": 10,
		"database.retry_period":       3,
		"database.debug":              false,
		constants.ETCDPathVK:          integrationetcd.Prefix,
		"keystone.local":              true,
		"keystone.assignment.type":    "static",
		"keystone.assignment.data":    keystoneAssignment(),
		"keystone.store.type":         "memory",
		"keystone.store.expire":       3600,
		"keystone.insecure":           true,
		"log_level":                   c.LogLevel,
		"server.notify_etcd":          c.EnableEtcdNotifier,
		"server.read_timeout":         10,
		"server.write_timeout":        5,
		"server.log_api":              !c.DisableLogAPI,
		"server.log_body":             !c.DisableLogAPI,
		"server.static_files.public":  path.Join(c.RepoRootPath, "public"),
		"server.enable_vnc_neutron":   true,
		"tls.enabled":                 false,
	})
}

func rbacConfig(enableRBAC bool) string {
	if enableRBAC {
		return "rbac"
	}
	return ""
}

func keystoneAssignment() *keystone.StaticAssignment {
	a := keystone.StaticAssignment{
		Domains: map[string]*kstypes.Domain{
			DefaultDomainID: {
				ID:   DefaultDomainID,
				Name: DefaultDomainName,
			},
		},
		Projects: make(map[string]*kstypes.Project),
		Users:    make(map[string]*kstypes.User),
	}
	a.Projects[AdminProjectID] = &kstypes.Project{
		Domain: a.Domains[DefaultDomainID],
		ID:     AdminProjectID,
		Name:   AdminProjectName,
	}
	a.Projects[DemoProjectID] = &kstypes.Project{
		Domain: a.Domains[DefaultDomainID],
		ID:     DemoProjectID,
		Name:   DemoProjectName,
	}
	a.Projects[NeutronProjectID] = &kstypes.Project{
		Domain: a.Domains[DefaultDomainID],
		ID:     NeutronProjectID,
		Name:   NeutronProjectName,
	}
	a.Projects[ServiceProjectID] = &kstypes.Project{
		Domain: a.Domains[DefaultDomainID],
		ID:     ServiceProjectID,
		Name:   ServiceProjectName,
	}
	a.Users[AdminUserID] = &kstypes.User{
		Domain:   a.Domains[DefaultDomainID],
		ID:       AdminUserID,
		Name:     AdminUserName,
		Password: AdminUserPassword,
		Roles: []*kstypes.Role{
			{
				ID:      AdminRoleID,
				Name:    AdminRoleName,
				Project: a.Projects[AdminProjectID],
			},
			{
				ID:      NeutronRoleID,
				Name:    NeutronRoleName,
				Project: a.Projects[NeutronProjectID],
			},
		},
	}
	a.Users[KSAdminUserID] = &kstypes.User{
		Domain:   a.Domains[DefaultDomainID],
		ID:       KSAdminUserID,
		Name:     KSAdminUserName,
		Password: KSAdminUserPassword,
		Roles: []*kstypes.Role{
			{
				ID:      AdminRoleID,
				Name:    AdminRoleName,
				Project: a.Projects[AdminProjectID],
			},
		},
	}
	a.Users[BobUserID] = &kstypes.User{
		Domain:   a.Domains[DefaultDomainID],
		ID:       BobUserID,
		Name:     BobUserName,
		Password: BobUserPassword,
		Roles: []*kstypes.Role{
			{
				ID:      MemberRoleID,
				Name:    MemberRoleName,
				Project: a.Projects[DemoProjectID],
			},
		},
	}
	a.Users[ServiceUserID] = &kstypes.User{
		Domain:   a.Domains[DefaultDomainID],
		ID:       ServiceUserID,
		Name:     ServiceUserName,
		Password: ServiceUserPassword,
		Roles: []*kstypes.Role{
			{
				ID:      AdminRoleID,
				Name:    AdminRoleName,
				Project: a.Projects[ServiceProjectID],
			},
		},
	}
	return &a
}

func setViper(config map[string]interface{}) {
	for k, v := range config {
		viper.SetDefault(k, v)
	}
}

// URL returns server base URL.
func (s *APIServer) URL() string {
	return s.testServer.URL
}

// CloseT closes server.
func (s *APIServer) CloseT(t *testing.T) {
	s.log.Debug("Closing test API server")
	err := s.Close()
	assert.NoError(t, err, "closing API Server failed")

}

// Close closes server.
func (s *APIServer) Close() error {
	s.testServer.Close()
	return s.APIServer.Close()
}

// ForceProxyUpdate requests an immediate update of endpoints and waits for its completion.
func (s *APIServer) ForceProxyUpdate() {
	s.APIServer.Proxy.ForceUpdate()
}
