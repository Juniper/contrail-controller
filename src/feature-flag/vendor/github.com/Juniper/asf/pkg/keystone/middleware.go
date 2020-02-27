package keystone

import (
	"context"
	"crypto/tls"
	"net/http"
	"strings"

	"github.com/Juniper/asf/pkg/errutil"
	"github.com/databus23/keystone"
	"github.com/labstack/echo"
	"github.com/pkg/errors"
	"github.com/sirupsen/logrus"
	"google.golang.org/grpc"
	"google.golang.org/grpc/metadata"

	asfauth "github.com/Juniper/asf/pkg/auth"
	cleanhttp "github.com/hashicorp/go-cleanhttp"
)

// AuthMiddleware provides HTTP and GRPC middleware to authenticate requests with Keystone.
type AuthMiddleware struct {
	auth      *keystone.Auth
	skipPaths []string
}

// NewAuthMiddleware returns an AuthMiddleware.
func NewAuthMiddleware(authURL string, insecure bool, skipPaths []string) *AuthMiddleware {
	return &AuthMiddleware{
		auth:      newKeystoneAuth(authURL, insecure),
		skipPaths: skipPaths,
	}
}

type HandlerFunc = func(echo.Context) error

// HTTPMiddleware is a keystone v3 authentication middleware for REST API.
//nolint: gocyclo
func (p *AuthMiddleware) HTTPMiddleware(next HandlerFunc) HandlerFunc {
	return func(c echo.Context) error {
		for _, pathQuery := range p.skipPaths {
			switch c.Request().URL.Path {
			case "/":
				return next(c)
			default:
				if strings.Contains(pathQuery, "?") {
					paths := strings.Split(pathQuery, "?")
					if strings.Contains(c.Request().URL.Path, paths[0]) &&
						strings.Compare(c.Request().URL.RawQuery, paths[1]) == 0 {
						return next(c)
					}
				} else if strings.Contains(c.Request().URL.Path, pathQuery) {
					return next(c)
				}
			}
		}
		r := c.Request()
		if r.ProtoMajor == 2 && strings.Contains(r.Header.Get("Content-Type"), "application/grpc") {
			// Skip grpc
			return next(c)
		}
		tokenString := r.Header.Get("X-Auth-Token")
		if tokenString == "" {
			cookie, _ := r.Cookie("x-auth-token") // nolint: errcheck
			if cookie != nil {
				tokenString = cookie.Value
			}
			if tokenString == "" {
				tokenString = c.QueryParam("auth_token")
			}
		}
		ctx, err := authenticate(r.Context(), p.auth, tokenString)
		if err != nil {
			logrus.Errorf("Authentication failure: %s", err)
			return errutil.ToHTTPError(err)
		}
		newRequest := r.WithContext(ctx)
		c.SetRequest(newRequest)
		return next(c)
	}
}

// GRPCInterceptor for Auth process for gRPC based apps.
func (p *AuthMiddleware) GRPCInterceptor(
	ctx context.Context, req interface{},
	info *grpc.UnaryServerInfo, handler grpc.UnaryHandler,
) (interface{}, error) {
	md, ok := metadata.FromIncomingContext(ctx)
	if !ok {
		return nil, errutil.ErrorUnauthenticated
	}
	token := md["x-auth-token"]
	if len(token) == 0 {
		return nil, errutil.ErrorUnauthenticated
	}
	newCtx, err := authenticate(ctx, p.auth, token[0])
	if err != nil {
		return nil, err
	}
	return handler(newCtx, req)
}

func newKeystoneAuth(authURL string, insecure bool) *keystone.Auth {
	a := keystone.New(authURL)
	a.Client = &http.Client{
		Transport: httpTransport(insecure),
	}
	return a
}

func httpTransport(insecure bool) *http.Transport {
	t := cleanhttp.DefaultPooledTransport()
	t.TLSClientConfig = &tls.Config{InsecureSkipVerify: insecure}
	return t
}

func authenticate(ctx context.Context, ka *keystone.Auth, tokenString string) (context.Context, error) {
	if tokenString == "" {
		return nil, errors.Wrap(errutil.ErrorUnauthenticated, "no auth token in request")
	}
	validatedToken, err := ka.Validate(tokenString)
	if err != nil {
		logrus.Errorf("Invalid Token: %s", err)
		return nil, errutil.ErrorUnauthenticated
	}
	roles := []string{}
	for _, r := range validatedToken.Roles {
		roles = append(roles, r.Name)
	}
	project := validatedToken.Project
	if project == nil {
		logrus.Debug("No project in a token")
		return nil, errutil.ErrorUnauthenticated
	}
	domain := validatedToken.Project.Domain.ID
	user := validatedToken.User

	objPerms := NewObjPerms(validatedToken)
	authContext := NewAuthIdentity(
		domain, project.ID, user.ID, roles, WithToken(tokenString), WithObjectPerms(objPerms),
	)

	return asfauth.WithIdentity(ctx, authContext), nil
}
