package apiserver

import (
	"encoding/json"
	"io"
	"io/ioutil"
	"path/filepath"
	"strings"
	"time"

	"github.com/Juniper/asf/pkg/keystone"
	"github.com/Juniper/asf/pkg/logutil"
	"github.com/labstack/echo"
	"github.com/labstack/echo/middleware"
	"github.com/pkg/errors"
	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	"google.golang.org/grpc"

	protocodec "github.com/gogo/protobuf/codec"
)

// Server is an HTTP and GRPC API server.
type Server struct {
	Echo *echo.Echo
	log  *logrus.Entry
}

// APIPlugin registers HTTP endpoints and GRPC services in Server.
type APIPlugin interface {
	RegisterHTTPAPI(HTTPRouter)
	RegisterGRPCAPI(GRPCRouter)
}

// HTTPRouter allows registering HTTP endpoints.
type HTTPRouter interface {
	GET(path string, h HandlerFunc, m ...RouteOption)
	POST(path string, h HandlerFunc, m ...RouteOption)
	PUT(path string, h HandlerFunc, m ...RouteOption)
	DELETE(path string, h HandlerFunc, m ...RouteOption)
	Group(prefix string, m ...RouteOption)
	// TODO(Witaut): Remove Use() - separate API endpoint plugins and middleware.
	Use(m ...MiddlewareFunc)
}

// HandlerFunc handles an HTTP request.
// This is a type alias so that users can make one without importing this package if needed.
// TODO(Witaut): Don't require importing echo to make a HandlerFunc.
type HandlerFunc = func(echo.Context) error

// MiddlewareFunc returns a HandlerFunc that processes a request, possibly leaving further processing to next.
// This is a type alias so that users can make one without importing this package if needed.
type MiddlewareFunc = func(next HandlerFunc) HandlerFunc

// RouteOption modifies RouteOptions.
type RouteOption func(*RouteOptions)

// RouteOptions specifies options for handling a route.
type RouteOptions struct {
	noAuth        bool
	middleware    []MiddlewareFunc
	homepageEntry linkDetails
}

// WithNoAuth makes requests to the route skip authentication.
func WithNoAuth() RouteOption {
	return func(rp *RouteOptions) {
		rp.noAuth = true
	}
}

// WithMiddleware adds middleware to pass requests to the route through.
func WithMiddleware(m ...MiddlewareFunc) RouteOption {
	return func(rp *RouteOptions) {
		rp.middleware = append(rp.middleware, m...)
	}
}

// WithNoHomepageMethod makes the route's entry in the homepage have no "method" field.
func WithNoHomepageMethod() RouteOption {
	return func(rp *RouteOptions) {
		rp.homepageEntry.Method = nil
	}
}

// WithHomepageName makes the route's entry in the homepage have the given endpoint name.
func WithHomepageName(name string) RouteOption {
	return func(rp *RouteOptions) {
		rp.homepageEntry.Name = name
	}
}

// WithHomepageType makes the route's entry in the homepage have the given endpoint type.
func WithHomepageType(t EndpointType) RouteOption {
	return func(rp *RouteOptions) {
		rp.homepageEntry.Rel = string(t)
	}
}

// EndpointType is the value of the "rel" field in a homepage entry.
type EndpointType string

// Endpoint types.
const (
	CollectionEndpoint   EndpointType = "collection"
	ResourceBaseEndpoint EndpointType = "resource-base"
	ActionEndpoint       EndpointType = "action"
	ProxyEndpoint        EndpointType = "proxy"
)

// GRPCRouter allows registering GRPC services.
type GRPCRouter interface {
	RegisterService(description *grpc.ServiceDesc, service interface{})
}

// NewServer makes a new Server.
// TODO(Witaut): Accept noAuthPaths with a server.AddNoAuthPaths() method
// instead of an argument to NewServer().
func NewServer(plugins []APIPlugin, noAuthPaths []string) (*Server, error) {
	s := &Server{
		Echo: echo.New(),
	}

	if err := logutil.Configure(viper.GetString("log_level")); err != nil {
		return nil, err
	}
	s.log = logutil.NewLogger("api-server")

	s.setupLoggingMiddleware()

	if viper.GetBool("server.enable_gzip") {
		s.Echo.Use(middleware.Gzip())
	}

	s.Echo.Use(middleware.Recover())
	s.Echo.Binder = &customBinder{}

	readTimeout := viper.GetInt("server.read_timeout")
	writeTimeout := viper.GetInt("server.write_timeout")
	s.Echo.Server.ReadTimeout = time.Duration(readTimeout) * time.Second
	s.Echo.Server.WriteTimeout = time.Duration(writeTimeout) * time.Second

	s.setupCORS()

	r := &httpRouter{
		echo:     s.Echo,
		homepage: NewHomepageHandler(),
	}
	for _, plugin := range plugins {
		plugin.RegisterHTTPAPI(r)
	}
	noAuthPaths = append(noAuthPaths, r.noAuthPaths...)

	staticFiles := viper.GetStringMapString("server.static_files")
	for prefix, root := range staticFiles {
		s.Echo.Static(prefix, root)
	}

	staticFilePaths, err := staticFilePaths(staticFiles)
	if err != nil {
		return nil, err
	}
	noAuthPaths = append(noAuthPaths, staticFilePaths...)

	httpMiddleware, authGRPCOpts := s.authMiddleware(noAuthPaths)
	r.Use(httpMiddleware...)

	if err := s.setupGRPC(authGRPCOpts, plugins); err != nil {
		return nil, err
	}

	if viper.GetBool("homepage.enabled") {
		s.Echo.GET("/", r.homepage.Handle)
	}

	if viper.GetBool("recorder.enabled") {
		s.Echo.Use(recorderMiddleware(s.log))
	}

	return s, nil
}

func (s *Server) setupLoggingMiddleware() {
	// TODO: integrate Echo's logger with logrus
	if viper.GetBool("server.log_api") {
		s.Echo.Use(middleware.Logger())
	} else {
		s.Echo.Logger.SetOutput(ioutil.Discard) // Disables Echo's built-in logging.
	}

	if !viper.GetBool("server.log_body") {
		return
	}
	s.Echo.Use(middleware.BodyDump(func(c echo.Context, requestBody, responseBody []byte) {
		if len(responseBody) > 10000 {
			responseBody = responseBody[0:10000] // trim too long entries
		}
		s.log.WithFields(logrus.Fields{
			"request-body":  string(requestBody),
			"response-body": string(responseBody),
		}).Debug("HTTP request handled")
	}))
}

type customBinder struct{}

func (*customBinder) Bind(i interface{}, c echo.Context) (err error) {
	rq := c.Request()
	ct := rq.Header.Get(echo.HeaderContentType)
	err = echo.ErrUnsupportedMediaType
	if !strings.HasPrefix(ct, echo.MIMEApplicationJSON) {
		db := new(echo.DefaultBinder)
		return db.Bind(i, c)
	}

	dec := json.NewDecoder(rq.Body)
	dec.UseNumber()
	err = dec.Decode(i)
	if err == io.EOF {
		return nil
	}
	return err
}

func (s *Server) setupCORS() {
	cors := viper.GetString("server.cors")
	if cors == "" {
		return
	}

	s.log.WithField("cors", cors).Debug("Enabling CORS")
	if cors == "*" {
		s.log.Warn("cors for * have security issue. DO NOT USE THIS IN PRODUCTION")
	}
	s.Echo.Use(middleware.CORSWithConfig(middleware.CORSConfig{
		AllowOrigins:  []string{cors},
		AllowMethods:  []string{echo.GET, echo.PUT, echo.POST, echo.DELETE},
		AllowHeaders:  []string{"X-Auth-Token", "Content-Type"},
		ExposeHeaders: []string{"X-Total-Count"},
	}))
}

func staticFilePaths(configuredStaticFiles map[string]string) ([]string, error) {
	var paths []string
	for prefix, root := range configuredStaticFiles {
		if prefix == "/" {
			staticFiles, err := ioutil.ReadDir(root)
			if err != nil {
				return nil, errors.WithStack(err)
			}
			for _, staticFile := range staticFiles {
				paths = append(paths, filepath.Join(prefix, staticFile.Name()))
			}
		} else {
			paths = append(paths, prefix)
		}
	}
	return paths, nil
}

func (s *Server) setupGRPC(grpcOpts []grpc.ServerOption, plugins []APIPlugin) error {
	if !viper.GetBool("server.enable_grpc") {
		return nil
	}
	if !viper.GetBool("server.tls.enabled") {
		return errors.New("GRPC support requires TLS configuration")
	}

	s.log.Debug("Enabling gRPC server")

	opts := []grpc.ServerOption{
		// TODO(Michal): below option potentially breaks compatibility for non golang grpc clients.
		// Ensure it doesn't or find a better solution for un/marshaling `oneof` fields properly.
		grpc.CustomCodec(protocodec.New(0)),
	}
	opts = append(opts, grpcOpts...)
	server := grpc.NewServer(opts...)

	for _, plugin := range plugins {
		plugin.RegisterGRPCAPI(server)
	}

	s.Echo.Use(gRPCMiddleware(server))

	return nil
}

func (s *Server) authMiddleware(noAuthPaths []string) (httpMiddleware []MiddlewareFunc, grpcOpts []grpc.ServerOption) {
	authURL := viper.GetString("keystone.authurl")
	if authURL != "" {
		insecure := viper.GetBool("keystone.insecure")

		m := keystone.NewAuthMiddleware(authURL, insecure, noAuthPaths)
		httpMiddleware = append(httpMiddleware, m.HTTPMiddleware)
		grpcOpts = append(grpcOpts, grpc.UnaryInterceptor(m.GRPCInterceptor))
	} else if viper.GetBool("no_auth") {
		httpMiddleware = append(httpMiddleware, noAuthHTTPMiddleware)
		grpcOpts = append(grpcOpts, grpc.UnaryInterceptor(noAuthGRPCInterceptor))
	}
	return httpMiddleware, grpcOpts
}

type httpRouter struct {
	echo        *echo.Echo
	homepage    *HomepageHandler
	noAuthPaths []string
}

// GET registers a GET handler.
func (r *httpRouter) GET(path string, h HandlerFunc, options ...RouteOption) {
	r.Add(echo.GET, path, h, options...)
}

// POST registers a POST handler.
func (r *httpRouter) POST(path string, h HandlerFunc, options ...RouteOption) {
	r.Add(echo.POST, path, h, options...)
}

// PUT registers a PUT handler.
func (r *httpRouter) PUT(path string, h HandlerFunc, options ...RouteOption) {
	r.Add(echo.PUT, path, h, options...)
}

// DELETE registers a DELETE handler.
func (r *httpRouter) DELETE(path string, h HandlerFunc, options ...RouteOption) {
	r.Add(echo.DELETE, path, h, options...)
}

// Add registers a handler for a route.
func (r *httpRouter) Add(method string, path string, h HandlerFunc, options ...RouteOption) {
	ro := makeRouteOptions(options, method, path)
	r.applyRouteOptions(ro, path)
	r.echo.Add(method, path, echo.HandlerFunc(h), echoMiddleware(ro.middleware)...)
}

// Group makes the middleware specified by options run for all requests with paths starting with prefix.
func (r *httpRouter) Group(prefix string, options ...RouteOption) {
	ro := makeRouteOptions(options, "", prefix)
	r.applyRouteOptions(ro, prefix)
	r.echo.Group(prefix, echoMiddleware(ro.middleware)...)
}

func makeRouteOptions(options []RouteOption, method, path string) (ro RouteOptions) {
	ro.homepageEntry = defaultHomepageEntry(method, path)
	for _, option := range options {
		option(&ro)
	}
	return ro
}

func (r *httpRouter) applyRouteOptions(ro RouteOptions, path string) {
	if ro.noAuth {
		r.noAuthPaths = append(r.noAuthPaths, path)
	}
	r.homepage.Register(ro.homepageEntry)
}

// Use makes middleware run for all requests.
func (r *httpRouter) Use(m ...MiddlewareFunc) {
	r.echo.Use(echoMiddleware(m)...)
}

func echoMiddleware(ms []MiddlewareFunc) []echo.MiddlewareFunc {
	echoMiddleware := make([]echo.MiddlewareFunc, 0, len(ms))
	for _, m := range ms {
		echoMiddleware = append(echoMiddleware, echoMiddlewareFunc(m))
	}
	return echoMiddleware
}

func echoMiddlewareFunc(m MiddlewareFunc) echo.MiddlewareFunc {
	return func(next echo.HandlerFunc) echo.HandlerFunc {
		return echo.HandlerFunc(m(HandlerFunc(next)))
	}
}

// Run starts serving the APIs to clients.
func (s *Server) Run() error {
	if viper.GetBool("server.tls.enabled") {
		return s.Echo.StartTLS(
			viper.GetString("server.address"),
			viper.GetString("server.tls.cert_file"),
			viper.GetString("server.tls.key_file"),
		)
	}

	return s.Echo.Start(viper.GetString("server.address"))
}
