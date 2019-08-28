package config

import (
    "encoding/json"
)

type VirtualRouter struct {
    *ContrailConfigObject
    VirtualRouterIpAddress string `json:"prop:virtual_router_ip_address"`
    VirtualRouterDpdkEnabled bool `json:"prop:virtual_router_dpdk_enabled"`
    VirtualMachineRefs []Ref `json:"virtual_machine_refs"`
}

func (o *VirtualRouter) AddRef(obj *ContrailConfigObject) {
    ref := Ref{
        Uuid: obj.Uuid, Type:obj.Type, Attr:map[string]interface{} {"attr":"",},
    }
    switch obj.Type{
        case "virtual_machine":
            o.VirtualMachineRefs = append(o.VirtualMachineRefs, ref)
    }
    o.UpdateDB()
}

func NewVirtualRouter(name, ip string) (*VirtualRouter, error) {
    co, err := createContrailConfigObject("virtual_router", name, "global-system-config",[]string{"default-global-system-config",name})
    if err != nil {
        return nil, err
    }
    o := &VirtualRouter{
        ContrailConfigObject: co,
        VirtualRouterIpAddress: ip,
        VirtualRouterDpdkEnabled: false,

    }

    err = o.UpdateDB()
    if err != nil {
        return nil, err
    }
    return o, nil
}

func (o *VirtualRouter) UpdateDB() error {
    b, err := json.Marshal(o)
    if err != nil {
        return err
    }
    UUIDTable[o.Uuid], err = o.ToJson(b)
    return err
}
