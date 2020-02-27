package apiserver

import (
	"context"

	"github.com/Juniper/asf/pkg/auth"
	"github.com/labstack/echo"
	"google.golang.org/grpc"
)

func noAuthHTTPMiddleware(next HandlerFunc) HandlerFunc {
	return func(c echo.Context) error {
		r := c.Request()
		ctx := auth.NoAuth(r.Context())
		newRequest := r.WithContext(ctx)
		c.SetRequest(newRequest)
		return next(c)
	}
}

func noAuthGRPCInterceptor(
	ctx context.Context, req interface{},
	info *grpc.UnaryServerInfo, handler grpc.UnaryHandler,
) (interface{}, error) {
	newCtx := auth.NoAuth(ctx)
	return handler(newCtx, req)
}
