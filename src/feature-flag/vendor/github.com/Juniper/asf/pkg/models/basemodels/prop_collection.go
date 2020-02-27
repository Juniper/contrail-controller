package basemodels

import (
	"fmt"
	"strings"

	"github.com/pkg/errors"
)

// PropCollectionUpdate operations.
const (
	PropCollectionUpdateOperationAdd    = "add"
	PropCollectionUpdateOperationModify = "modify"
	PropCollectionUpdateOperationSet    = "set"
	PropCollectionUpdateOperationDelete = "delete"
)

// PropCollectionUpdate holds update data for collection property (with CollectionType "map" or "list").
type PropCollectionUpdate struct {
	Field     string
	Operation string
	Value     interface{}
	Position  interface{}
}

// PositionForList parses position and validates operation for ListProperty collection update.
func (u *PropCollectionUpdate) PositionForList() (position int, err error) {
	op := strings.ToLower(u.Operation)
	switch op {
	case PropCollectionUpdateOperationAdd:
		if u.Value == nil {
			return 0, errors.Errorf("add operation needs value")
		}
	case PropCollectionUpdateOperationModify:
		if u.Value == nil {
			return 0, errors.Errorf("modify operation needs value")
		}
		p, ok := u.Position.(int32)
		if !ok {
			return 0, errors.New("modify operation needs position")
		}
		position = int(p)
	case PropCollectionUpdateOperationDelete:
		p, ok := u.Position.(int32)
		if !ok {
			return 0, errors.New("delete operation needs position")
		}
		position = int(p)
	default:
		return 0, errors.Errorf("unsupported operation: %s", u.Operation)
	}
	return position, nil
}

// KeyForMap validates MapProperty collection update.
func (u *PropCollectionUpdate) KeyForMap() (key string, err error) {
	op := strings.ToLower(u.Operation)
	switch op {
	case PropCollectionUpdateOperationSet:
		if u.Value == nil {
			return "", errors.Errorf("set operation needs value")
		}
	case PropCollectionUpdateOperationDelete:
		if key = fmt.Sprint(u.Position); u.Position == nil || key == "" {
			return "", errors.New("delete operation needs position")
		}
	default:
		return "", errors.Errorf("unsupported operation: %s", u.Operation)
	}
	return key, nil
}
