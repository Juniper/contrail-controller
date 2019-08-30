/*
 * Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
 */

package config

import (
    "encoding/json"
)

// This represents a RoutingInstance contrail configuration object. This eventually maps to a network table (aka vrf). import/export RouteTargets can be associated using references to RouteTarget objects (RouteTargetRefs).
type RoutingInstance struct {
    *ContrailConfigObject
    IsDefault bool `json:"prop:routing_instance_is_default"`
    RouteTargetRefs []Ref `json:"route_target_refs"`
}

func (r *RoutingInstance) AddRef(uuidTable *UUIDTableType, obj *ContrailConfigObject) {
    switch obj.Type{
        case "route_target":
            ref := Ref{
              Uuid: obj.Uuid, Type:obj.Type,
              Attr:map[string]interface{} {"attr":"",},
            }
            r.RouteTargetRefs = append(r.RouteTargetRefs, ref)
    }
    r.UpdateDB(uuidTable)
}

func NewRoutingInstance(fqNameTable *FQNameTableType, uuidTable *UUIDTableType, name string) (*RoutingInstance, error) {
    c, err := createContrailConfigObject(fqNameTable, "routing_instance", name, "virtual_network", []string{"default-domain", "default-project", name, name})
    if err != nil {
        return nil, err
    }
    r := &RoutingInstance{
        ContrailConfigObject: c,
        IsDefault: false,
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
    (*uuidTable)[r.Uuid], err = r.ToJson(bytes)
    return err
}
