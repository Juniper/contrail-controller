package services

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"

	"github.com/gogo/protobuf/types"
	"github.com/labstack/echo"
	"github.com/pkg/errors"

	"github.com/Juniper/asf/pkg/errutil"
	"github.com/Juniper/asf/pkg/models"
	"github.com/Juniper/asf/pkg/models/basemodels"
)

// RESTSetTag handles set-tag request.
func (service *ContrailService) RESTSetTag(c echo.Context) error {
	var rawJSON map[string]json.RawMessage
	if err := c.Bind(&rawJSON); err != nil {
		return echo.NewHTTPError(http.StatusBadRequest, fmt.Sprintf("invalid JSON format: %v", err))
	}

	setTag := &SetTagRequest{}
	if err := setTag.parseObjFields(rawJSON); err != nil {
		return errutil.ToHTTPError(err)
	}
	if err := setTag.parseTagAttrs(rawJSON); err != nil {
		return errutil.ToHTTPError(err)
	}

	ctx := c.Request().Context()
	err := service.InTransactionDoer.DoInTransaction(ctx, func(ctx context.Context) error {
		_, err := service.SetTag(ctx, setTag)
		return err
	})

	if err != nil {
		return errutil.ToHTTPError(err)
	}
	return c.JSON(http.StatusOK, map[string]interface{}{})
}

// SetTag allows setting tags based on SetTagRequest.
func (service *ContrailService) SetTag(ctx context.Context, setTag *SetTagRequest) (*types.Empty, error) {
	if err := setTag.validate(); err != nil {
		return nil, err
	}

	obj, err := GetObject(ctx, service.Next(), setTag.ObjType, setTag.ObjUUID)
	if err != nil {
		return nil, errutil.ErrorBadRequestf(
			"error: %v, while getting %v with UUID %v", err, setTag.ObjType, setTag.ObjUUID,
		)
	}

	references := obj.GetTagReferences()

	for _, tagAttr := range setTag.Tags {
		if references, err = service.handleTagAttr(ctx, tagAttr, obj, references); err != nil {
			return nil, err
		}
	}
	e, err := NewEvent(EventOption{
		Data:      map[string]interface{}{"tag_refs": references.Unique()},
		Kind:      obj.Kind(),
		UUID:      obj.GetUUID(),
		Operation: OperationUpdate,
	})
	if err != nil {
		return nil, err
	}

	_, err = e.Process(ctx, service)

	return &types.Empty{}, err
}

func (service *ContrailService) handleTagAttr(
	ctx context.Context, tagAttr *SetTagAttr, obj basemodels.Object, refs basemodels.References,
) (basemodels.References, error) {
	switch {
	case tagAttr.isDeleteRequest():
		return removeTagsOfType(refs, tagAttr.GetType()), nil
	case tagAttr.hasTypeUniquePerObject():
		refs = removeTagsOfType(refs, tagAttr.GetType())

		uuid, err := service.getTagUUIDInScope(ctx, tagAttr.GetType(), tagAttr.GetValue().GetValue(), tagAttr.IsGlobal, obj)

		return append(refs, basemodels.NewUUIDReference(uuid, models.KindTag)), err
	case tagAttr.hasAddValues():
		for _, tagValue := range tagAttr.AddValues {
			uuid, err := service.getTagUUIDInScope(ctx, tagAttr.GetType(), tagValue, tagAttr.IsGlobal, obj)
			if err != nil {
				return nil, err
			}

			refs = append(refs, basemodels.NewUUIDReference(uuid, models.KindTag))
		}
		return refs, nil
	case tagAttr.hasDeleteValues():
		toDelete := map[string]bool{}
		for _, tagValue := range tagAttr.DeleteValues {
			uuid, err := service.getTagUUIDInScope(ctx, tagAttr.GetType(), tagValue, tagAttr.IsGlobal, obj)
			if err != nil {
				return nil, err
			}

			toDelete[uuid] = true
		}
		return refs.Filter(func(r basemodels.Reference) bool {
			return !toDelete[r.GetUUID()]
		}), nil
	default:
		return refs, nil
	}
}

func removeTagsOfType(r basemodels.References, tagType string) basemodels.References {
	return r.Filter(func(ref basemodels.Reference) bool {
		tType, _ := models.TagTypeValueFromFQName(ref.GetTo())
		return tType != tagType
	})
}

// TagLocator is an object that references a tag and helps determining tag scope.
type TagLocator interface {
	GetUUID() string
	GetFQName() []string
	GetPerms2() *models.PermType2
	GetParentType() string
	Kind() string
}

func cannotDetermineTagScopeError(tagName string) error {
	return errutil.ErrorNotFoundf("Not able to determine the scope of the tag '%s'", tagName)
}

func (service *ContrailService) getTagFQNameInScope(
	ctx context.Context, tagName string, isGlobal bool, obj basemodels.Object,
) ([]string, error) {
	tl, ok := obj.(TagLocator)
	if !ok {
		return nil, cannotDetermineTagScopeError(tagName)
	}

	switch {
	case isGlobal:
		return []string{tagName}, nil
	case tl.Kind() == "project":
		return basemodels.ChildFQName(tl.GetFQName(), tagName), nil
	case tl.GetParentType() == "project" && len(tl.GetFQName()) > 1:
		fqName := tl.GetFQName()
		fqName[len(fqName)-1] = tagName
		return fqName, nil
	case tl.GetPerms2() != nil:
		data, err := service.MetadataGetter.GetMetadata(
			ctx, basemodels.Metadata{UUID: tl.GetPerms2().GetOwner()},
		)
		if err != nil {
			return nil, errors.Wrapf(err, "cannot find %s %s owner", tagName, tl.GetUUID())
		}
		return basemodels.ChildFQName(data.FQName, tagName), nil
	default:
		return nil, cannotDetermineTagScopeError(tagName)
	}
}

func (service *ContrailService) getTagUUIDInScope(
	ctx context.Context, tagType, tagValue string, isGlobal bool, obj basemodels.Object,
) (string, error) {
	tagName := models.CreateTagName(tagType, tagValue)

	fqName, err := service.getTagFQNameInScope(ctx, tagName, isGlobal, obj)
	if err != nil {
		return "", err
	}

	m, err := service.MetadataGetter.GetMetadata(
		ctx, basemodels.Metadata{FQName: fqName, Type: models.KindTag},
	)
	if err != nil {
		return "", errors.Wrapf(err, "not able to determine the scope of the tag %s", tagName)
	}
	return m.UUID, nil
}
