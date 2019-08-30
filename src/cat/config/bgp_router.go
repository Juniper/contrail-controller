/*
 * copyright (c) 2019 juniper networks, inc. all rights reserved.
 */

package config

import (
    "encoding/json"
    "cat/types"
)

// This represents a BgpRouter contrail-configuration object. Router parameters are part of BgpRouterParameters. BGP neigbborship is automatically determined based on the referred other BgpRouter objects as listed in BgpRouterRefs.
type BgpRouter struct {
    *ContrailConfigObject
    BgpRouterParameters types.BgpRouterParams `json:"prop:bgp_router_parameters"`
    BgpRouterRefs []Ref `json:"bgp_router_refs"`
}

func (b *BgpRouter) AddRef(uuidTable *UUIDTableType, obj *ContrailConfigObject) {
    session_attributes := types.BgpSession {
        Attributes: []types.BgpSessionAttributes{{
            AdminDown: false,
            Passive: false,
            AsOverride: false,
            LoopCount: 0,
        }},
    }
    ref := Ref{
        Uuid: obj.Uuid,
        Type:obj.Type,
        Attr:map[string]interface{} { "attr": session_attributes, },
    }
    switch obj.Type{
        case "bgp_router":
            b.BgpRouterRefs = append(b.BgpRouterRefs, ref)
    }
    b.UpdateDB(uuidTable)
}

func NewBGPRouter(fqNameTable *FQNameTableType, uuidTable *UUIDTableType, name, address string, port int) (*BgpRouter, error) {
    co, err := createContrailConfigObject(fqNameTable, "bgp_router", name, "routing_instance",[]string{"default-domain", "default-project", "ip-fabric", "__default__", name})
    if err != nil {
        return nil, err
    }

    bgp_router_paramters := types.BgpRouterParams{
        AdminDown: false,
        Vendor: "contrail",
        AutonomousSystem: 64512,
        Identifier: address,
        Address: address,
        Port: port,
        LocalAutonomousSystem: 64512,
        RouterType: "control-node",
        AddressFamilies: &types.AddressFamilies{
            Family: []string{"route-target", "inet-vpn", "e-vpn", "erm-vpn", "inet6-vpn",},
        },
    }
    b := &BgpRouter{
        ContrailConfigObject: co,
        BgpRouterParameters: bgp_router_paramters,
    }

    if err := b.UpdateDB(uuidTable); err != nil {
        return nil, err
    }
    return b, nil
}

func (b *BgpRouter) UpdateDB(uuidTable *UUIDTableType) error {
    bytes, err := json.Marshal(b)
    if err != nil {
        return err
    }
    (*uuidTable)[b.Uuid], err = b.ToJson(bytes)
    return err
}
