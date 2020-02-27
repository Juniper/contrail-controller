package services

import (
	"context"
	"fmt"
	"net/http"

	"github.com/labstack/echo"
	"github.com/pkg/errors"

	"github.com/Juniper/asf/pkg/errutil"
	"github.com/Juniper/asf/pkg/models/basemodels"
)

// FQNameToIDService interface.
type FQNameToIDService interface {
	FQNameToID(context.Context, *FQNameToIDRequest) (*FQNameToIDResponse, error)
}

// RESTFQNameToUUID is a REST handler for translating FQName to UUID
func (svc *ContrailService) RESTFQNameToUUID(c echo.Context) error {
	var request *FQNameToIDRequest

	err := c.Bind(&request)
	if err != nil {
		return echo.NewHTTPError(http.StatusBadRequest, "Invalid JSON format")
	}

	response, err := svc.FQNameToID(c.Request().Context(), request)
	if err != nil {
		//TODO adding Project
		return errutil.ToHTTPError(err)
	}
	return c.JSON(http.StatusOK, response)
}

// FQNameToID translates FQName to corresponding UUID stored in database
func (svc *ContrailService) FQNameToID(
	ctx context.Context,
	request *FQNameToIDRequest,
) (*FQNameToIDResponse, error) {
	metadata, err := svc.MetadataGetter.GetMetadata(ctx, basemodels.Metadata{Type: request.Type, FQName: request.FQName})
	if err != nil {
		//TODO adding Project
		errMsg := fmt.Sprintf("Failed to retrieve metadata for FQName %v and Type %v", request.FQName, request.Type)
		return nil, errors.Wrapf(err, errMsg)
	}

	//TODO permissions check

	return &FQNameToIDResponse{
		UUID: metadata.UUID,
	}, nil
}
