package services

import (
	"encoding/json"
	"fmt"
	"net/http"
	strings "strings"

	"github.com/gogo/protobuf/types"
	"github.com/labstack/echo"

	"github.com/Juniper/asf/pkg/errutil"
	"github.com/Juniper/asf/pkg/models"
	"github.com/Juniper/asf/pkg/models/basemodels"
)

var (
	// TagTypeNotUniquePerObject contains not unique tag-types per object
	TagTypeNotUniquePerObject = map[string]bool{
		"label": true,
	}
	// TagTypeAuthorizedOnAddressGroup contains authorized on address group tag-types
	TagTypeAuthorizedOnAddressGroup = map[string]bool{
		"label": true,
	}
)

func (a *SetTagAttr) isDeleteRequest() bool {
	return a.Value == nil && (len(a.AddValues) == 0 && len(a.DeleteValues) == 0)
}

func (a *SetTagAttr) hasAddValues() bool {
	return len(a.AddValues) > 0
}

func (a *SetTagAttr) hasDeleteValues() bool {
	return len(a.DeleteValues) > 0
}

func (a *SetTagAttr) hasTypeUniquePerObject() bool {
	return !TagTypeNotUniquePerObject[a.GetType()]
}

func (t *SetTagRequest) validate() error {
	if t.ObjUUID == "" || t.ObjType == "" {
		return errutil.ErrorBadRequestf(
			"both obj_uuid and obj_type should be specified but got uuid: '%s' and type: '%s",
			t.ObjUUID, t.ObjType,
		)
	}
	for _, tagAttr := range t.Tags {
		if err := tagAttr.validate(t.ObjType); err != nil {
			return err
		}
	}
	return nil
}

func (a *SetTagAttr) validate(objType string) error {
	tagType := strings.ToLower(a.Type)

	// address-group object can only be associated with label
	if objType == "address_group" && !TagTypeAuthorizedOnAddressGroup[tagType] {
		return errutil.ErrorBadRequestf(
			"invalid tag type %v for object type %v", tagType, objType,
		)
	}
	if a.hasTypeUniquePerObject() {
		if len(a.AddValues) > 0 || len(a.DeleteValues) > 0 {
			return errutil.ErrorBadRequestf(
				"tag type %v cannot be set multiple times on a same object", tagType,
			)
		}
		if a.Value == nil && !a.isDeleteRequest() {
			return errutil.ErrorBadRequestf("no valid value provided for tag type %v", tagType)
		}
	}
	return nil
}

func (t *SetTagRequest) parseObjFields(rawJSON map[string]json.RawMessage) error {
	if err := parseField(rawJSON, "obj_uuid", &t.ObjUUID); err != nil {
		return err
	}
	if err := parseField(rawJSON, "obj_type", &t.ObjType); err != nil {
		return err
	}

	return nil
}

func parseField(rawJSON map[string]json.RawMessage, key string, dst interface{}) error {
	if val, ok := rawJSON[key]; ok {
		if err := json.Unmarshal(val, dst); err != nil {
			return errutil.ErrorBadRequestf("invalid '%s' format: %v", key, err)
		}
		delete(rawJSON, key)
	}
	return nil
}

func (t *SetTagRequest) parseTagAttrs(rawJSON map[string]json.RawMessage) error {
	for key, val := range rawJSON {
		wrapper := struct {
			SetTagAttr
			Value interface{} `json:"value"`
		}{SetTagAttr: SetTagAttr{
			Type: strings.ToLower(key),
		}}
		if err := json.Unmarshal(val, &wrapper); err != nil {
			return echo.NewHTTPError(http.StatusBadRequest, fmt.Sprintf("invalid '%v' format: %v", key, err))
		}

		if val, ok := wrapper.Value.(string); ok {
			wrapper.SetTagAttr.Value = &types.StringValue{Value: val}
		}
		t.Tags = append(t.Tags, &wrapper.SetTagAttr)
	}
	return nil
}

func (t *SetTagRequest) tagRefEvent(tagUUID string, operation RefOperation) (*Event, error) {
	return NewRefUpdateEvent(RefUpdateOption{
		ReferenceType: basemodels.ReferenceKind(t.ObjType, models.KindTag),
		FromUUID:      t.ObjUUID,
		ToUUID:        tagUUID,
		Operation:     operation,
	})
}
