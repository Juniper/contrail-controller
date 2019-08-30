/*
 * copyright (c) 2019 juniper networks, inc. all rights reserved.
 */

package config

import (
    "encoding/json"
)

// VirtualRouter represents a compute node that runs contrail-vouter-agent and connects to control-node over xmpp.
type VirtualRouter struct {
    *ContrailConfig
    VirtualRouterIpAddress string `json:"prop:virtual_router_ip_address"`
    VirtualRouterDpdkEnabled bool `json:"prop:virtual_router_dpdk_enabled"`
    VirtualMachineRefs []Ref `json:"virtual_machine_refs"`
}

func (o *VirtualRouter) AddRef(uuidTable *UUIDTableType, obj *ContrailConfig) {
    ref := Ref{
        UUID: obj.UUID, Type:obj.Type, Attr:map[string]interface{} {"attr":"",},
    }
    switch obj.Type{
        case "virtual_machine":
            o.VirtualMachineRefs = append(o.VirtualMachineRefs, ref)
    }
    o.UpdateDB(uuidTable)
}

func NewVirtualRouter(fqNameTable *FQNameTableType, uuidTable *UUIDTableType, name, ip string) (*VirtualRouter, error) {
    co, err := createContrailConfig(fqNameTable, "virtual_router", name, "global-system-config",[]string{"default-global-system-config",name})
    if err != nil {
        return nil, err
    }
    o := &VirtualRouter{
        ContrailConfig: co,
        VirtualRouterIpAddress: ip,
        VirtualRouterDpdkEnabled: false,

    }

    err = o.UpdateDB(uuidTable)
    if err != nil {
        return nil, err
    }
    return o, nil
}

func (o *VirtualRouter) UpdateDB(uuidTable *UUIDTableType) error {
    b, err := json.Marshal(o)
    if err != nil {
        return err
    }
    (*uuidTable)[o.UUID], err = o.ToJson(b)
    return err
}
