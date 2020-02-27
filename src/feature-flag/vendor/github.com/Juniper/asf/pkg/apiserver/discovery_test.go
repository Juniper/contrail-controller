package apiserver

import (
	"net/http"
	"net/http/httptest"
	"testing"

	"github.com/labstack/echo"
	"github.com/stretchr/testify/assert"
)

func TestDiscovery(t *testing.T) {
	type register struct {
		method  string
		path    string
		options []RouteOption
	}

	tests := []struct {
		host      string
		registers []register
		expected  string
	}{
		{
			"addr",
			[]register{
				{
					method:  "GET",
					path:    "/path1",
					options: []RouteOption{WithHomepageName("test1"), WithHomepageType("rel1")},
				},
				{
					method:  "",
					path:    "path2",
					options: []RouteOption{WithHomepageName("test2"), WithHomepageType("rel2")},
				},
			},
			`{
				"href": "http://addr",
				"links": [
					{"link": { "href": "http://addr/path1", "method": "GET", "name": "test1", "rel": "rel1" }},
					{"link": { "href": "http://addr/path2", "method": null, "name": "test2", "rel": "rel2" }}
				]
			}`,
		},
		{
			"localhost:8082",
			[]register{
				{
					method:  "GET",
					path:    "/path1",
					options: []RouteOption{WithHomepageName("test1"), WithHomepageType("rel1")},
				},
				{
					method:  "",
					path:    "path2",
					options: []RouteOption{WithHomepageName("test2"), WithHomepageType("rel2")},
				},
			},
			`{
				"href": "http://localhost:8082",
				"links": [
					{"link": { "href": "http://localhost:8082/path1", "method": "GET", "name": "test1", "rel": "rel1" }},
					{"link": { "href": "http://localhost:8082/path2", "method": null, "name": "test2", "rel": "rel2" }}
				]
			}`,
		},

		{
			"addr",
			[]register{
				{
					method: "POST",
					path:   "/resources",
					options: []RouteOption{
						WithNoHomepageMethod(),
						WithHomepageName("resource"),
						WithHomepageType(CollectionEndpoint),
					},
				},
				{
					method: "GET",
					path:   "/resources",
					options: []RouteOption{
						WithNoHomepageMethod(),
						WithHomepageName("resource"),
						WithHomepageType(CollectionEndpoint),
					},
				},
			},
			`{
				"href": "http://addr",
				"links": [
					{"link": { "href": "http://addr/resources", "method": null, "name": "resource", "rel": "collection" }}
				]
			}`,
		},

		{
			"addr",
			[]register{
				{
					method: "PUT",
					path:   "/resource/:id",
				},
				{
					method: "GET",
					path:   "/resource/:id",
				},
				{
					method: "DELETE",
					path:   "/resource/:id",
				},
			},
			`{
				"href": "http://addr",
				"links": [
					{"link": { "href": "http://addr/resource", "method": "PUT", "name": "resource", "rel": "resource-base" }},
					{"link": { "href": "http://addr/resource", "method": "GET", "name": "resource", "rel": "resource-base" }},
					{"link": { "href": "http://addr/resource", "method": "DELETE", "name": "resource", "rel": "resource-base" }}
				]
			}`,
		},

		{
			"addr",
			[]register{
				{
					method: "PUT",
					path:   "/resource/:id",
					options: []RouteOption{
						WithNoHomepageMethod(),
					},
				},
				{
					method: "GET",
					path:   "/resource/:id",
					options: []RouteOption{
						WithNoHomepageMethod(),
					},
				},
				{
					method: "DELETE",
					path:   "/resource/:id",
					options: []RouteOption{
						WithNoHomepageMethod(),
					},
				},
			},
			`{
				"href": "http://addr",
				"links": [
					{"link": { "href": "http://addr/resource", "method": null, "name": "resource", "rel": "resource-base" }}
				]
			}`,
		},

		{
			"addr",
			[]register{
				{
					method: "POST",
					path:   "/some-action",
				},
			},
			`{
				"href": "http://addr",
				"links": [
					{"link": { "href": "http://addr/some-action", "method": "POST", "name": "some-action", "rel": "action" }}
				]
			}`,
		},

		{
			"addr",
			[]register{
				{
					method:  "GET",
					path:    "/keystone/v3/projects",
					options: []RouteOption{WithHomepageType(CollectionEndpoint)},
				},
				{
					method: "GET",
					path:   "/keystone/v3/projects/:id",
				},
			},
			`{
				"href": "http://addr",
				"links": [
					{"link": { "href": "http://addr/keystone/v3/projects", "method": "GET", "name": "keystone/v3/projects", "rel": "collection" }},
					{"link": { "href": "http://addr/keystone/v3/projects", "method": "GET", "name": "keystone/v3/projects", "rel": "resource-base" }}
				]
			}`,
		},

		{
			"addr",
			[]register{
				{
					method: "POST",
					path:   "/neutron/:type",
					options: []RouteOption{
						WithHomepageName("neutron plugin"),
					},
				},
			},
			`{
				"href": "http://addr",
				"links": [
					{"link": { "href": "http://addr/neutron/:type", "method": "POST", "name": "neutron plugin", "rel": "action" }}
				]
			}`,
		},
	}

	for _, tt := range tests {
		dh := NewHomepageHandler()

		for _, r := range tt.registers {
			options := makeRouteOptions(r.options, r.method, r.path)
			dh.Register(options.homepageEntry)
		}

		e := echo.New()
		rec := httptest.NewRecorder()
		req := httptest.NewRequest(echo.GET, "/", nil)
		req.Host = tt.host
		c := e.NewContext(req, rec)
		err := dh.Handle(c)

		if assert.NoError(t, err) {
			assert.Equal(t, http.StatusOK, rec.Code)
			assert.JSONEq(t, tt.expected, rec.Body.String())
		}
	}
}
