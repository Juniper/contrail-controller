package proxy

import (
	"bufio"
	"net"
	"net/http"
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestResponseBufferHeader(t *testing.T) {
	for _, tt := range []struct {
		name             string
		rbhModifications map[string][]string
		modifiedRBH      http.Header
	}{
		{
			name:        "returns header containing data of wrapped response writer",
			modifiedRBH: newTestHeader(),
		},
		{
			name: "returns copy of wrapped response writer's header",
			rbhModifications: map[string][]string{
				"firstKey": {"modified"},
				"thirdKey": {"new"},
			},
			modifiedRBH: http.Header{
				"firstKey":  []string{"modified"},
				"secondKey": []string{"one", "two"},
				"thirdKey":  []string{"new"},
			},
		},
	} {
		t.Run(tt.name, func(t *testing.T) {
			rw := &dummyResponseWriterHijacker{
				header: newTestHeader(),
			}
			rb := newResponseBuffer(rw)

			rbh := rb.Header()

			assert.Equal(t, newTestHeader(), rbh, "invalid header returned")

			for k, v := range tt.rbhModifications {
				rbh[k] = v
			}
			assert.Equal(t, tt.modifiedRBH, rbh, "invalid header returned")
			assert.Equal(t, tt.modifiedRBH, rb.Header(), "the same header instance should be returned")
			assert.Equal(t, newTestHeader(), rw.header, "response writer header should not be modified")
		})
	}
}

func newTestHeader() http.Header {
	return http.Header{
		"firstKey":  []string{"foo", "bar"},
		"secondKey": []string{"one", "two"},
	}
}

type dummyResponseWriterHijacker struct {
	header http.Header
}

// Header dummy implementation.
func (rw *dummyResponseWriterHijacker) Header() http.Header {
	return rw.header
}

// Write dummy implementation.
func (rw *dummyResponseWriterHijacker) Write(data []byte) (int, error) {
	return 0, nil
}

// WriteHeader dummy implementation.
func (rw *dummyResponseWriterHijacker) WriteHeader(statusCode int) {
	return
}

// Hijack dummy implementation.
func (rw *dummyResponseWriterHijacker) Hijack() (net.Conn, *bufio.ReadWriter, error) {
	return nil, nil, nil
}
