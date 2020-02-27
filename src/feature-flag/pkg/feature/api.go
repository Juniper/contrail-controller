package feature

import (
	"fmt"
	"net/http"

	"github.com/Juniper/asf/pkg/apiserver"
	"github.com/labstack/echo"
)

//API Path definitions.
const (
	CreateFlagPath = "create-flag"
)

// FeatureFlagAPIPlugin allows managing feature flags.
type FeatureFlagAPIPlugin struct{}

// RegisterHTTPAPI registers the api endpoints.
func (FeatureFlagAPIPlugin) RegisterHTTPAPI(r apiserver.HTTPRouter) {
	r.POST(CreateFlagPath, RESTCreateFlag)
}

// RegisterGRPCAPI does nothing, as the API is HTTP-only.
func (FeatureFlagAPIPlugin) RegisterGRPCAPI(r apiserver.GRPCRouter) {
}

func RESTCreateFlag(c echo.Context) error {
	// Read form fields
	name := c.FormValue("name")
	code := c.FormValue("code")
	state := c.FormValue("state")

	return c.HTML(http.StatusOK, fmt.Sprintf("<p>Feature %s created successfully with fields code=%s and state=%s.</p>", name, code, state))
}
