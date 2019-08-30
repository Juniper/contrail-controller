/*
 * copyright (c) 2019 juniper networks, inc. all rights reserved.
 */

package config

import (
    "cat/types"
    "encoding/json"
    "github.com/google/uuid"
)

// VirtualNetwork represents an isolated tenant virtual network. It is a place holder for routes of different address families such as ipv4, ipv6, mac, etc.
type VirtualNetwork struct {
    *ContrailConfig
    NetworkIpamRefs []Ref `json:"network_ipam_refs"`
    RoutingInstanceChildren []Child `json:"routing_instance_children"`
}

func (o *VirtualNetwork) AddChild(uuidTable *UUIDTableType, obj *ContrailConfig) {
    switch obj.Type{
        case "routing_instance":
            child := Child{ UUID: obj.UUID, Type:obj.Type }
            o.RoutingInstanceChildren = append(o.RoutingInstanceChildren, child)
    }
    o.UpdateDB(uuidTable)
}

func (o *VirtualNetwork) AddRef(uuidTable *UUIDTableType, obj *ContrailConfig) {
    switch obj.Type{
        case "network_ipam":
            u, _ := uuid.NewUUID()
            subnet := types.VnSubnetsType{
                IpamSubnets: []types.IpamSubnetType{
                types.IpamSubnetType {
                    Subnet: &types.SubnetType {
                        IpPrefix: "1.1.1.0",
                        IpPrefixLen: 24,
                    },
                    AddrFromStart: true,
                    EnableDhcp:true,
                    DefaultGateway: "1.1.1.1",
                    SubnetUuid: u.String(),
                    DnsServerAddress: "1.1.1.2",
                } },
            }
            ref := Ref{ UUID: obj.UUID, Type:obj.Type,
                        Attr:map[string]interface{} {"attr": subnet,},
            }
            o.NetworkIpamRefs = append(o.NetworkIpamRefs, ref)
    }
    o.UpdateDB(uuidTable)
}

func NewVirtualNetwork(fqNameTable *FQNameTableType, uuidTable *UUIDTableType, name string) (*VirtualNetwork, error) {
    co, err := createContrailConfig(fqNameTable, "virtual_network", name, "project", []string{"default-domain", "default-project", name})
    if err != nil {
        return nil, err
    }
    o := &VirtualNetwork{
        ContrailConfig: co,
    }

    err = o.UpdateDB(uuidTable)
    if err != nil {
        return nil, err
    }
    return o, nil
}

func (o *VirtualNetwork) UpdateDB(uuidTable *UUIDTableType) error {
    b, err := json.Marshal(o)
    if err != nil {
        return err
    }
    (*uuidTable)[o.UUID], err = o.ToJson(b)
    return err
}
