package basemodels

import (
	"github.com/gogo/protobuf/proto"
)

// Object is generic model instance.
type Object interface {
	proto.Message
	GetUUID() string
	SetUUID(string)
	GetFQName() []string
	GetParentUUID() string
	GetParentType() string
	Kind() string
	GetReferences() References
	GetTagReferences() References
	GetBackReferences() []Object
	GetChildren() []Object
	SetHref(string)
	AddReference(interface{})
	AddBackReference(interface{})
	AddChild(interface{})
	RemoveReference(interface{})
	RemoveBackReference(interface{})
	RemoveChild(interface{})
	RemoveReferences()
	ToMap() map[string]interface{}
	ApplyMap(map[string]interface{}) error
	ApplyPropCollectionUpdate(*PropCollectionUpdate) (updated map[string]interface{}, err error)
}
