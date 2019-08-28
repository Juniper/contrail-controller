package config

import (
    "cat/types"
    "encoding/json"
    "github.com/google/uuid"
)

type VirtualNetwork struct {
    *ContrailConfigObject
    NetworkIpamRefs []Ref `json:"network_ipam_refs"`
    RoutingInstanceChildren []Child `json:"routing_instance_children"`
}

func (o *VirtualNetwork) AddChild(obj *ContrailConfigObject) {
    switch obj.Type{
        case "routing_instance":
            child := Child{ Uuid: obj.Uuid, Type:obj.Type }
            o.RoutingInstanceChildren = append(o.RoutingInstanceChildren, child)
    }
    o.UpdateDB()
}

func (o *VirtualNetwork) AddRef(obj *ContrailConfigObject) {
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
            ref := Ref{ Uuid: obj.Uuid, Type:obj.Type,
                        Attr:map[string]interface{} {"attr": subnet,},
            }
            o.NetworkIpamRefs = append(o.NetworkIpamRefs, ref)
    }
    o.UpdateDB()
}

func NewVirtualNetwork(name string) (*VirtualNetwork, error) {
    co, err := createContrailConfigObject("virtual_network", name, "project", []string{"default-domain", "default-project", name})
    if err != nil {
        return nil, err
    }
    o := &VirtualNetwork{
        ContrailConfigObject: co,
    }

    err = o.UpdateDB()
    if err != nil {
        return nil, err
    }
    return o, nil
}

func (o *VirtualNetwork) UpdateDB() error {
    b, err := json.Marshal(o)
    if err != nil {
        return err
    }
    UUIDTable[o.Uuid], err = o.ToJson(b)
    return err
}
