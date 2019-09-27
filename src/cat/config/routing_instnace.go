package config

import (
	"encoding/json"
)

// RoutingInstance represents contrail configuration object which is associated with a virtual-network. This eventually maps to a network table (aka vrf). import/export RouteTargets can be associated using references to RouteTarget objects (RouteTargetRefs).
type RoutingInstance struct {
	*ContrailConfig
	IsDefault       bool  `json:"prop:routing_instance_is_default"`
	RouteTargetRefs []Ref `json:"route_target_refs"`
}

func (r *RoutingInstance) AddRef(uuidTable *UUIDTableType, obj *ContrailConfig) {
	switch obj.Type {
	case "route_target":
		ref := Ref{
			UUID: obj.UUID, Type: obj.Type,
			Attr: map[string]interface{}{"attr": ""},
		}
		r.RouteTargetRefs = append(r.RouteTargetRefs, ref)
	}
	r.UpdateDB(uuidTable)
}

func NewRoutingInstance(fqNameTable *FQNameTableType, uuidTable *UUIDTableType, name string) (*RoutingInstance, error) {
	c, err := createContrailConfig(fqNameTable, "routing_instance", name, "virtual_network", []string{"default-domain", "default-project", name, name})
	if err != nil {
		return nil, err
	}
	r := &RoutingInstance{
		ContrailConfig: c,
		IsDefault:      false,
	}
	err = r.UpdateDB(uuidTable)
	if err != nil {
		return nil, err
	}
	return r, nil
}

func (r *RoutingInstance) UpdateDB(uuidTable *UUIDTableType) error {
	bytes, err := json.Marshal(r)
	if err != nil {
		return err
	}
	(*uuidTable)[r.UUID], err = r.Map(bytes)
	return err
}
