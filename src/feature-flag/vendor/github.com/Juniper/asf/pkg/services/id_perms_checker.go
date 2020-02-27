package services

import (
	"context"

	"github.com/pkg/errors"
	"github.com/sirupsen/logrus"
	
	"github.com/Juniper/asf/pkg/errutil"
	"github.com/Juniper/asf/pkg/models"
	// TODO(dfurman): Decouple from below packages
	//"github.com/Juniper/asf/pkg/auth"
)

// isVisibleObject verifies that the object is visible to a user without administrator rights
func isVisibleObject(ctx context.Context, idPerms *models.IdPermsType) error {
	if ctx == nil {
		logrus.Errorf("user unauthenticated: context is nil")
		return nil
	}
	auth := auth.GetAuthCTX(ctx)
	if auth == nil {
		logrus.Errorf("user unauthenticated: non authorized context")
		return nil
	}
	if idPerms == nil {
		// by default UserVisible == true
		return nil
	}
	if !idPerms.GetUserVisible() && !auth.IsAdmin() {
		return errors.Errorf("this object is not visible by users: %s", auth.UserID())
	}
	return nil
}

func getStoredIDPerms(
	ctx context.Context, service *ContrailService, typeName, uuid string,
) (*models.IdPermsType, error) {
	base, err := getObject(ctx, service.DBService, typeName, uuid,
		[]string{models.AccessControlListFieldIDPerms})
	if err != nil {
		return nil, err
	}
	type baseIDPermser interface {
		GetIDPerms() *models.IdPermsType
	}
	m, ok := base.(baseIDPermser)
	if !ok {
		return nil, errutil.ErrorInternalf("method IDPerms() not found")
	}
	return m.GetIDPerms(), nil
}
