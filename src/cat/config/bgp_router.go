/*
 * copyright (c) 2019 juniper networks, inc. all rights reserved.
 */

package config

import (
    "encoding/json"
    "cat/types"
)

type BgpRouter struct {
    *ContrailConfigObject
    BgpRouterParameters types.BgpRouterParams `json:"prop:bgp_router_parameters"`
    BgpRouterRefs []Ref `json:"bgp_router_refs"`
}

func (o *BgpRouter) AddRef(obj *ContrailConfigObject) {
    session_attributes := types.BgpSession {
        Attributes: []types.BgpSessionAttributes{{
            AdminDown: false,
            Passive: false,
            AsOverride: false,
            LoopCount: 0,
        },},
    }
    ref := Ref{
        Uuid: obj.Uuid,
        Type:obj.Type,
        Attr:map[string]interface{} { "attr": session_attributes, },
    }
    switch obj.Type{
        case "bgp_router":
            o.BgpRouterRefs = append(o.BgpRouterRefs, ref)
    }
    o.UpdateDB()
}

func NewBgpRouter(name, address string, port int) (*BgpRouter, error) {
    co, err := createContrailConfigObject("bgp_router", name, "routing_instance",[]string{"default-domain", "default-project", "ip-fabric", "__default__", name})
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
            Family: []string{
                "route-target", "inet-vpn", "e-vpn", "erm-vpn", "inet6-vpn",
            },
        },
    }
    o := &BgpRouter{
        ContrailConfigObject: co,
        BgpRouterParameters: bgp_router_paramters,
    }

    err = o.UpdateDB()
    if err != nil {
        return nil, err
    }
    return o, nil
}

func (o *BgpRouter) UpdateDB() error {
    b, err := json.Marshal(o)
    if err != nil {
        return err
    }
    UUIDTable[o.Uuid], err = o.ToJson(b)
    return err
}
