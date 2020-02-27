package baseservices

import "context"

type servicesContextKey string

const (
	// internalRequestKey used in context as additional propetry
	internalRequestKey servicesContextKey = "isInternal"
)

// WithInternalRequest creates child context with additional information
// that this context is for internal requests
func WithInternalRequest(ctx context.Context) context.Context {
	return context.WithValue(ctx, internalRequestKey, true)
}

// IsInternalRequest checks if context is for internal request
func IsInternalRequest(ctx context.Context) bool {
	if v := ctx.Value(internalRequestKey); v != nil {
		return v.(bool)
	}

	return false
}
