package config

import (
    "encoding/json"
)

type RoutingInstance struct {
    *ContrailConfigObject
    IsDefault bool `json:"prop:routing_instance_is_default"`
    RouteTargetRefs []Ref `json:"route_target_refs"`
}

func (o *RoutingInstance) AddRef(obj *ContrailConfigObject) {
    switch obj.Type{
        case "route_target":
            ref := Ref{
              Uuid: obj.Uuid, Type:obj.Type,
              Attr:map[string]interface{} {"attr":"",},
            }
            o.RouteTargetRefs = append(o.RouteTargetRefs, ref)
    }
    o.UpdateDB()
}

func NewRoutingInstance(name string) (*RoutingInstance, error) {
    r, err := createContrailConfigObject("routing_instance", name, "virtual_network", []string{"default-domain", "default-project", name, name})
    if err != nil {
        return nil, err
    }
    o := &RoutingInstance{
        ContrailConfigObject: r,
        IsDefault: false,
    }
    err = o.UpdateDB()
    if err != nil {
        return nil, err
    }
    return o, nil
}

func (o *RoutingInstance) UpdateDB() error {
    b, err := json.Marshal(o)
    if err != nil {
        return err
    }
    UUIDTable[o.Uuid], err = o.ToJson(b)
    return err
}
