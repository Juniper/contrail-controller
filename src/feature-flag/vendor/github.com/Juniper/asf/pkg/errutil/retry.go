package errutil

type temporary interface {
	Temporary() bool
}

type causer interface {
	Cause() error
}

// ShouldRetry checks if error could be temporary.
func ShouldRetry(err error) bool {
	if err == nil {
		return false
	}
	switch t := err.(type) {
	case temporary:
		return t.Temporary()
	case causer:
		return ShouldRetry(t.Cause())
	}
	return false
}
