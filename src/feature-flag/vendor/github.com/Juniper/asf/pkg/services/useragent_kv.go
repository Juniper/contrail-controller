package services

import (
	"context"
	"fmt"
	"net/http"

	"github.com/gogo/protobuf/types"
	"github.com/labstack/echo"

	"github.com/Juniper/asf/pkg/errutil"
	"github.com/Juniper/asf/pkg/models"
)

// UserAgent key value store operations.
const (
	UserAgentKVOperationStore    = "STORE"
	UserAgentKVOperationRetrieve = "RETRIEVE"
	UserAgentKVOperationDelete   = "DELETE"
)

type userAgentKVRequest map[string]interface{}

// RESTUserAgentKV is a REST handler for UserAgentKV requests.
func (svc *ContrailService) RESTUserAgentKV(c echo.Context) error {
	var data userAgentKVRequest
	if err := c.Bind(&data); err != nil {
		return echo.NewHTTPError(http.StatusBadRequest, "Invalid JSON format")
	}

	if err := data.validateKVRequest(); err != nil {
		return errutil.ToHTTPError(err)
	}

	switch op := data["operation"]; op {
	case UserAgentKVOperationStore:
		return svc.storeKeyValue(c, data["key"].(string), data["value"].(string))
	case UserAgentKVOperationRetrieve:
		if key, ok := data["key"].(string); ok && key != "" {
			return svc.retrieveValue(c, key)
		}

		if keys, ok := data["key"].([]string); ok && len(keys) != 0 {
			return svc.retrieveValues(c, keys)
		}

		return svc.retrieveKVPs(c)
	case UserAgentKVOperationDelete:
		return svc.deleteKey(c, data["key"].(string))
	}

	return nil
}

func (svc *ContrailService) storeKeyValue(c echo.Context, key string, value string) error {
	if _, err := svc.StoreKeyValue(c.Request().Context(), &StoreKeyValueRequest{
		Key:   key,
		Value: value,
	}); err != nil {
		return errutil.ToHTTPError(err)
	}
	return c.NoContent(http.StatusOK)
}

func (svc *ContrailService) retrieveValue(c echo.Context, key string) error {
	kv, err := svc.RetrieveValues(
		c.Request().Context(),
		&RetrieveValuesRequest{Keys: []string{key}},
	)
	if err != nil {
		return errutil.ToHTTPError(err)
	}

	if len(kv.Values) == 0 {
		return echo.NewHTTPError(http.StatusNotFound, fmt.Sprintf("No user agent key: %v", key))
	}

	return c.JSON(http.StatusOK, map[string]string{"value": kv.Values[0]})
}

func (svc *ContrailService) retrieveValues(c echo.Context, keys []string) error {
	response, err := svc.RetrieveValues(c.Request().Context(), &RetrieveValuesRequest{Keys: keys})
	if err != nil {
		return errutil.ToHTTPError(err)
	}
	return c.JSON(http.StatusOK, response)
}

func (svc *ContrailService) retrieveKVPs(c echo.Context) error {
	response, err := svc.RetrieveKVPs(c.Request().Context(), &types.Empty{})
	if err != nil {
		return errutil.ToHTTPError(err)
	}
	return c.JSON(http.StatusOK, response)
}

func (svc *ContrailService) deleteKey(c echo.Context, key string) error {
	if _, err := svc.DeleteKey(c.Request().Context(), &DeleteKeyRequest{Key: key}); err != nil {
		return errutil.ToHTTPError(err)
	}
	return c.NoContent(http.StatusOK)
}

func (data userAgentKVRequest) validateKVRequest() error {
	if _, ok := data["operation"]; !ok {
		return errutil.ErrorBadRequest("Key/value store API needs 'operation' parameter")
	}

	if _, ok := data["key"]; !ok {
		return errutil.ErrorBadRequest("Key/value store API needs 'key' parameter")
	}

	switch op := data["operation"]; op {
	case UserAgentKVOperationStore, UserAgentKVOperationDelete:
		return data.validateStoreOrDeleteOperation()
	case UserAgentKVOperationRetrieve:
		return data.validateRetrieveOperation()
	default:
		return errutil.ErrorNotFoundf("Invalid Operation %v", op)
	}
}

func (data userAgentKVRequest) validateRetrieveOperation() error {
	errMsg := "retrieve: 'key' must be a string or a list of strings"

	switch key := data["key"].(type) {
	case string:
	case []interface{}:
		keyStrings := make([]string, 0, len(key))
		for _, k := range key {
			if keyString, ok := k.(string); ok {
				keyStrings = append(keyStrings, keyString)
			} else {
				return errutil.ErrorBadRequestf(errMsg)
			}
		}
		data["key"] = keyStrings
	default:
		return errutil.ErrorBadRequestf(errMsg)
	}

	return nil
}

func (data userAgentKVRequest) validateStoreOrDeleteOperation() error {
	if key, ok := data["key"].(string); !ok {
		return errutil.ErrorBadRequestf("store/delete: 'key' must be a string")
	} else if key == "" {
		return errutil.ErrorBadRequestf("store/delete: 'key' must be nonempty")
	}

	return nil
}

// StoreKeyValue stores a value under given key.
// Updates the value if key is already present.
func (svc *ContrailService) StoreKeyValue(
	ctx context.Context,
	request *StoreKeyValueRequest,
) (*types.Empty, error) {
	return &types.Empty{}, svc.UserAgentKVService.StoreKeyValue(ctx, request.Key, request.Value)
}

// RetrieveValues retrieves values corresponding to the given list of keys.
// The values are returned in an arbitrary order. Keys not present in the store are ignored.
func (svc *ContrailService) RetrieveValues(
	ctx context.Context,
	request *RetrieveValuesRequest,
) (res *RetrieveValuesResponse, err error) {
	var values []string
	values, err = svc.UserAgentKVService.RetrieveValues(ctx, request.Keys)
	if err == nil {
		res = &RetrieveValuesResponse{Values: values}
	}
	return res, err
}

// DeleteKey deletes the value under the given key.
// Nothing happens if the key is not present.
func (svc *ContrailService) DeleteKey(
	ctx context.Context,
	request *DeleteKeyRequest,
) (*types.Empty, error) {
	return &types.Empty{}, svc.UserAgentKVService.DeleteKey(ctx, request.Key)
}

// RetrieveKVPs returns the entire store as a list of (key, value) pairs.
func (svc *ContrailService) RetrieveKVPs(
	ctx context.Context,
	request *types.Empty,
) (res *RetrieveKVPsResponse, err error) {
	var kvps []*models.KeyValuePair
	kvps, err = svc.UserAgentKVService.RetrieveKVPs(ctx)
	if err == nil {
		res = &RetrieveKVPsResponse{KeyValuePairs: kvps}
	}
	return res, err
}
