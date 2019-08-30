/*
 * Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
 */

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

func (o *VirtualMachineInterface) AddRef(uuidTable *UUIDTableType, obj *ContrailConfigObject) {
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
    o.UpdateDB(uuidTable)
}

func NewVirtualMachineInterface(fqNameTable *FQNameTableType, uuidTable *UUIDTableType, name string) (*VirtualMachineInterface, error) {
    co, err := createContrailConfigObject(fqNameTable, "virtual_machine_interface", name, "project", []string{"default-domain", "default-project", name})
    if err != nil {
        return nil, err
    }
    o := &VirtualMachineInterface{
        ContrailConfigObject: co,
    }
    err = o.UpdateDB(uuidTable)
    if err != nil {
        return nil, err
    }
    return o, nil
}

func (o *VirtualMachineInterface) UpdateDB(uuidTable *UUIDTableType) error {
    b, err := json.Marshal(o)
    if err != nil {
        return err
    }
    (*uuidTable)[o.Uuid], err = o.ToJson(b)
    return err
}
