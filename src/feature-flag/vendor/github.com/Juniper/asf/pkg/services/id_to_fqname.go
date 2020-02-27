package services

import (
	"context"

	"github.com/pkg/errors"

	"fmt"
	"net/http"

	"github.com/labstack/echo"

	"github.com/Juniper/asf/pkg/errutil"
	"github.com/Juniper/asf/pkg/models/basemodels"
)

// IDToFQNameService interface.
type IDToFQNameService interface {
	IDToFQName(context.Context, *IDToFQNameRequest) (*IDToFQNameResponse, error)
}

// RESTIDToFQName is a REST handler for translating UUID to FQName and Type
func (svc *ContrailService) RESTIDToFQName(c echo.Context) error {
	var request *IDToFQNameRequest

	if err := c.Bind(&request); err != nil {
		return echo.NewHTTPError(http.StatusBadRequest, fmt.Sprintf("invalid JSON format: %v", err))
	}

	response, err := svc.IDToFQName(c.Request().Context(), request)
	if err != nil {
		return errutil.ToHTTPError(err)
	}
	return c.JSON(http.StatusOK, response)
}

// IDToFQName translates UUID to corresponding FQName and Type stored in database
func (svc *ContrailService) IDToFQName(
	ctx context.Context,
	request *IDToFQNameRequest,
) (*IDToFQNameResponse, error) {
	metadata, err := svc.MetadataGetter.GetMetadata(ctx, basemodels.Metadata{UUID: request.UUID})
	if err != nil {
		return nil, errors.Wrapf(err, "failed to retrieve metadata for UUID %v", request.UUID)
	}

	return &IDToFQNameResponse{
		Type:   metadata.Type,
		FQName: metadata.FQName,
	}, nil
}
