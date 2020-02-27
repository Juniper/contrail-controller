package apiserver

import (
	"net/http"
	"strings"

	"github.com/labstack/echo"
)

// HomepageHandler which serves a set of registered links.
type HomepageHandler struct {
	links []*link
}

type link struct {
	Link linkDetails `json:"link"`
}

type linkDetails struct {
	Path   string  `json:"href"`
	Method *string `json:"method"`
	Name   string  `json:"name"`
	Rel    string  `json:"rel"`
}

func defaultHomepageEntry(method, path string) linkDetails {
	rel := ActionEndpoint
	if strings.HasSuffix(path, "/:id") {
		rel = ResourceBaseEndpoint
		path = strings.TrimSuffix(path, "/:id")
	}
	path = strings.TrimPrefix(path, "/")
	entry := linkDetails{
		Path: path,
		Name: path,
		Rel:  string(rel),
	}
	if method != "" {
		entry.Method = &method
	}
	return entry
}

// NewHomepageHandler creates a new HomepageHandler.
func NewHomepageHandler() *HomepageHandler {
	return &HomepageHandler{}
}

// Register adds a new link to the HomepageHandler.
func (h *HomepageHandler) Register(l linkDetails) {
	if existingLink, ok := h.get(l); ok && existingLink.Method == nil {
		return
	}
	h.links = append(h.links, &link{Link: l})
}

func (h *HomepageHandler) get(link linkDetails) (*linkDetails, bool) {
	for _, l := range h.links {
		if l.Link.Path == link.Path {
			return &l.Link, true
		}
	}
	return nil, false
}

// Handle requests to return the links.
func (h *HomepageHandler) Handle(c echo.Context) error {
	r := c.Request()
	addr := GetRequestSchema(r) + r.Host

	var reply struct {
		Addr  string  `json:"href"`
		Links []*link `json:"links"`
	}

	reply.Addr = addr
	for _, l := range h.links {
		reply.Links = append(reply.Links, &link{
			Link: linkDetails{
				Path:   strings.Join([]string{addr, l.Link.Path}, "/"),
				Method: l.Link.Method,
				Name:   l.Link.Name,
				Rel:    l.Link.Rel,
			},
		})
	}

	return c.JSON(http.StatusOK, reply)
}

// GetRequestSchema returns 'https://' for TLS based request or 'http://' otherwise
func GetRequestSchema(r *http.Request) string {
	if r.TLS != nil {
		return "https://"
	}
	return "http://"
}
