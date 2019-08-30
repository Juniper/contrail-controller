/*
 * Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
 */

package config

import (
    "encoding/json"
)

type InstanceIp struct {
    *ContrailConfigObject
    InstanceIpAddress string `json:"prop:instance_ip_address"`
    InstanceIpFamily string `json:"prop:instance_ip_family"`
    VirtualNetworkRefs []Ref `json:"virtual_network_refs"`
    VirtualMachineInterfaceRefs []Ref `json:"virtual_machine_interface_refs"`
}

func (o *InstanceIp) AddRef(uuidTable *UUIDTableType, obj *ContrailConfigObject) {
    ref := Ref{
        Uuid: obj.Uuid, Type:obj.Type, Attr:map[string]interface{} {"attr":"",},
    }
    switch obj.Type{
        case "virtual_network":
            o.VirtualNetworkRefs = append(o.VirtualNetworkRefs, ref)
        case "virtual_machine_interface":
            o.VirtualMachineInterfaceRefs =
                append(o.VirtualMachineInterfaceRefs, ref)
    }
    o.UpdateDB(uuidTable)
}

func NewInstanceIp(fqNameTable *FQNameTableType, uuidTable *UUIDTableType, name, address, family string) (*InstanceIp, error) {
    ip, err := createContrailConfigObject(fqNameTable, "instance_ip", name, "", []string{name})
    o := &InstanceIp{
        ContrailConfigObject: ip,
        InstanceIpAddress: address,
        InstanceIpFamily: family,

    }

    err = o.UpdateDB(uuidTable)
    if err != nil {
        return nil, err
    }
    return o, nil
}

func (o *InstanceIp) UpdateDB(uuidTable *UUIDTableType) error {
    b, err := json.Marshal(o)
    if err != nil {
        return err
    }
    (*uuidTable)[o.Uuid], err = o.ToJson(b)
    return err
}
