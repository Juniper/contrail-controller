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

func (o *InstanceIp) AddRef(obj *ContrailConfigObject) {
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
    o.UpdateDB()
}

func NewInstanceIp(name, address, family string) (*InstanceIp, error) {
    ip, err := createContrailConfigObject("instance_ip", name, "", []string{name})
    o := &InstanceIp{
        ContrailConfigObject: ip,
        InstanceIpAddress: address,
        InstanceIpFamily: family,

    }

    err = o.UpdateDB()
    if err != nil {
        return nil, err
    }
    return o, nil
}

func (o *InstanceIp) UpdateDB() error {
    b, err := json.Marshal(o)
    if err != nil {
        return err
    }
    UUIDTable[o.Uuid], err = o.ToJson(b)
    return err
}
