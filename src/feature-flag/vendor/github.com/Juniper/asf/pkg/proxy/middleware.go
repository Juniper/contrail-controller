package proxy

import (
	"bufio"
	"bytes"
	"crypto/tls"
	"io/ioutil"
	"net"
	"net/http"
	"net/http/httputil"
	"net/url"

	"github.com/labstack/echo"
	"github.com/pkg/errors"
	"github.com/sirupsen/logrus"

	cleanhttp "github.com/hashicorp/go-cleanhttp"
)

// TODO(dfurman): move rest of dynamicProxyMiddleware here

const (
	skipServerCertificateVerification = true // TODO: add "insecure" field to endpoint schema
)

// HandleRequest proxies request from given context to first available target URL and
// returns response to client.
func HandleRequest(ctx echo.Context, rawTargetURLs []string, log *logrus.Entry) error {
	rb := &responseBuffer{}
	body, err := ioutil.ReadAll(ctx.Request().Body)
	if err != nil {
		return errors.Wrap(err, "reading request body")
	}

	for i, rawTargetURL := range rawTargetURLs {
		targetURL, pErr := url.Parse(rawTargetURL)
		if pErr != nil {
			logrus.WithError(pErr).WithField("target-url", targetURL).Error("Failed to parse target URL - ignoring")
			continue
		}

		rp := httputil.NewSingleHostReverseProxy(targetURL)
		rp.Transport = newTransport()
		// TODO(dfurman): mblotniak's suggestion below
		// Use ReverseProxy's ModifyResponse() and ErrorHandler() instead of responseBuffer.
		// The ModifyResponse function should return error if response status is 502 or 503.
		// ErrorHandler should be changed to handle those errors by notifying this for instead of the user.
		rb = newResponseBuffer(ctx.Response())
		setBody(ctx.Request(), body)
		rp.ServeHTTP(rb, ctx.Request())
		if rb.Status() < 500 || rb.Status() > 599 {
			break
		}

		e := log.WithFields(logrus.Fields{
			"last-response-status": rb.Status(),
			"last-target-url":      rawTargetURL,
			"target-urls":          rawTargetURLs,
		})
		if i < (len(rawTargetURLs) - 1) {
			e.Debug("Target server unavailable - retrying request to next target")
		} else {
			e.Info("All target servers unavailable")
		}
	}
	if err = rb.FlushToRW(); err != nil {
		return errors.Wrap(err, "flush response buffer to response writer")
	}

	return nil
}

func newTransport() *http.Transport {
	t := cleanhttp.DefaultPooledTransport()
	t.TLSClientConfig = &tls.Config{InsecureSkipVerify: skipServerCertificateVerification}
	return t
}

func setBody(r *http.Request, body []byte) {
	r.Body = ioutil.NopCloser(bytes.NewReader(body))
	r.ContentLength = int64(len(body))
}

// responseBuffer wraps response writer and allows postponing writing response to it.
// Wrapped response writer needs to implement http.ResponseWriter and http.Hijacker interfaces.
type responseBuffer struct {
	rw         responseWriterHijacker
	statusCode int
	header     http.Header
	data       *bytes.Buffer
}

func newResponseBuffer(rw responseWriterHijacker) *responseBuffer {
	return &responseBuffer{
		rw:     rw,
		header: cloneHeader(rw.Header()),
		data:   &bytes.Buffer{},
	}
}

// cloneHeader returns a copy of h or nil if h is nil.
// TODO(dfurman): use http.Header.Clone() when Go version is updated to 1.13.
func cloneHeader(h http.Header) http.Header {
	if h == nil {
		return nil
	}

	// Find total number of values.
	nv := 0
	for _, vv := range h {
		nv += len(vv)
	}
	sv := make([]string, nv) // shared backing array for headers' values
	h2 := make(http.Header, len(h))
	for k, vv := range h {
		n := copy(sv, vv)
		h2[k] = sv[:n:n]
		sv = sv[n:]
	}
	return h2
}

type responseWriterHijacker interface {
	http.ResponseWriter
	http.Hijacker
}

// Header returns the header map of wrapped response writer that will be sent by WriteHeader.
func (rb *responseBuffer) Header() http.Header {
	return rb.header
}

// Write writes given data to the buffer.
func (rb *responseBuffer) Write(data []byte) (int, error) {
	return rb.data.Write(data)
}

// WriteHeader sets status code field with given status code.
func (rb *responseBuffer) WriteHeader(statusCode int) {
	rb.statusCode = statusCode
}

// Hijack calls hijacks connection of wrapped response writer.
func (rb *responseBuffer) Hijack() (net.Conn, *bufio.ReadWriter, error) {
	return rb.rw.Hijack()
}

// Status returns status code written to buffer.
func (rb *responseBuffer) Status() int {
	return rb.statusCode
}

// FlushToRW writes the header and data to wrapped response writer.
// It is intentionally named different from Flush() method (see http.Flusher())
// to prevent premature buffer flushing triggered by other actors.
func (rb *responseBuffer) FlushToRW() error {
	copyHeader(rb.rw.Header(), rb.header)

	if rb.statusCode != 0 {
		rb.rw.WriteHeader(rb.statusCode)
	}

	_, err := rb.rw.Write(rb.data.Bytes())
	if err != nil {
		return errors.Wrap(err, "write the target's response to client")
	}
	return nil
}

func copyHeader(dst, src http.Header) {
	for k, vv := range src {
		for _, v := range vv {
			dst.Add(k, v)
		}
	}
}
