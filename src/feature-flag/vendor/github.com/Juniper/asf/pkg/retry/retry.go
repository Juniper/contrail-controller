package retry

import (
	"time"
)

// Func is a function that could be retried.
type Func func() (retry bool, err error)

// Option is a function that mutates config.
type Option func(*config)

type logger interface {
	Debugf(string, ...interface{})
}

type config struct {
	withLog  logger
	interval *time.Duration
}

func getConfig(opts []Option) config {
	c := config{}
	for _, o := range opts {
		o(&c)
	}
	return c
}

// WithLog adds additional logging in Retry.
func WithLog(log logger) Option {
	return func(c *config) {
		c.withLog = log
	}
}

// WithInterval adds additional logging in Retry.
func WithInterval(interval time.Duration) Option {
	return func(c *config) {
		c.interval = &interval
	}
}

// Do runs function f in loop until the function returns retry == false.
func Do(f Func, opts ...Option) error {
	c := getConfig(opts)

	for {
		retry, err := f()
		if !retry {
			return err
		}
		if log := c.withLog; log != nil {
			log.Debugf("Retrying, error was: %v", err)
		}
		if interval := c.interval; interval != nil {
			time.Sleep(*interval)
		}
	}
}
