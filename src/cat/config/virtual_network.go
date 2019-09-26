package config

import (
	"encoding/json"
	"github.com/google/uuid"

	"cat/types"
)

// VirtualNetwork represents an isolated tenant virtual network. It is a place holder for routes of different address families such as ipv4, ipv6, mac, etc.
type VirtualNetwork struct {
	*ContrailConfig
	NetworkIpamRefs         []Ref   `json:"network_ipam_refs"`
	RoutingInstanceChildren []Child `json:"routing_instance_children"`
}

func (vn *VirtualNetwork) AddChild(uuidTable *UUIDTableType, obj *ContrailConfig) {
	switch obj.Type {
	case "routing_instance":
		child := Child{UUID: obj.UUID, Type: obj.Type}
		vn.RoutingInstanceChildren = append(vn.RoutingInstanceChildren, child)
	}
	vn.UpdateDB(uuidTable)
}

func (vn *VirtualNetwork) AddRef(uuidTable *UUIDTableType, obj *ContrailConfig) {
	switch obj.Type {
	case "network_ipam":
		u, _ := uuid.NewUUID()
		subnet := types.VnSubnetsType{
			IpamSubnets: []types.IpamSubnetType{
				types.IpamSubnetType{
					Subnet: &types.SubnetType{
						IpPrefix:    "1.1.1.0",
						IpPrefixLen: 24,
					},
					AddrFromStart:    true,
					EnableDhcp:       true,
					DefaultGateway:   "1.1.1.1",
					SubnetUuid:       u.String(),
					DnsServerAddress: "1.1.1.2",
				}},
		}
		ref := Ref{UUID: obj.UUID, Type: obj.Type,
			Attr: map[string]interface{}{"attr": subnet},
		}
		vn.NetworkIpamRefs = append(vn.NetworkIpamRefs, ref)
	}
	vn.UpdateDB(uuidTable)
}

func NewVirtualNetwork(fqNameTable *FQNameTableType, uuidTable *UUIDTableType, name string) (*VirtualNetwork, error) {
	co, err := createContrailConfig(fqNameTable, "virtual_network", name, "project", []string{"default-domain", "default-project", name})
	if err != nil {
		return nil, err
	}
	vn := &VirtualNetwork{
		ContrailConfig: co,
	}

	err = vn.UpdateDB(uuidTable)
	if err != nil {
		return nil, err
	}
	return vn, nil
}

func (vn *VirtualNetwork) UpdateDB(uuidTable *UUIDTableType) error {
	b, err := json.Marshal(vn)
	if err != nil {
		return err
	}
	(*uuidTable)[vn.UUID], err = vn.Map(b)
	return err
}
