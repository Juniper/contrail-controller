package config

import (
    "encoding/json"
)

type VirtualMachineInterface struct {
    *ContrailConfigObject
    VirtualMachineRefs []Ref `json:"virtual_machine_refs"`
    VirtualNetworkRefs []Ref `json:"virtual_network_refs"`
    RoutingInstanceRefs []Ref `json:"routing_instance_refs"`
}

func (o *VirtualMachineInterface) AddRef(obj *ContrailConfigObject) {
    switch obj.Type{
        case "virtual_machine":
            ref := Ref{
                Uuid: obj.Uuid, Type:obj.Type,
                Attr:map[string]interface{} {"attr":"",},
            }
            o.VirtualMachineRefs = append(o.VirtualMachineRefs, ref)
        case "virtual_network":
            ref := Ref{
                Uuid: obj.Uuid, Type:obj.Type,
                Attr:map[string]interface{} {"attr":"",},
            }
            o.VirtualNetworkRefs = append(o.VirtualNetworkRefs, ref)
        case "routing_instance":
            ref := Ref{
                Uuid: obj.Uuid, Type:obj.Type,
                Attr:map[string]interface{} {"attr":nil, "is_weakref": false},
            }
            o.RoutingInstanceRefs = append(o.RoutingInstanceRefs, ref)
    }
    o.UpdateDB()
}

func NewVirtualMachineInterface(name string) (*VirtualMachineInterface, error) {
    co, err := createContrailConfigObject("virtual_machine_interface", name, "project", []string{"default-domain", "default-project", name})
    if err != nil {
        return nil, err
    }
    o := &VirtualMachineInterface{
        ContrailConfigObject: co,
    }
    err = o.UpdateDB()
    if err != nil {
        return nil, err
    }
    return o, nil
}

func (o *VirtualMachineInterface) UpdateDB() error {
    b, err := json.Marshal(o)
    if err != nil {
        return err
    }
    UUIDTable[o.Uuid], err = o.ToJson(b)
    return err
}
