package config

import (
	"encoding/json"

	"cat/types"
)

// BGPRouter represents a bgp router contrail-configuration object. Router parameters are part of BGPRouterParameters. BGP neigbborship is automatically determined based on the referred other BGPRouter objects as listed in BGPRouterRefs.
type BGPRouter struct {
	*ContrailConfig
	BGPRouterParameters types.BgpRouterParams `json:"prop:bgp_router_parameters"`
	BGPRouterRefs       []Ref                 `json:"bgp_router_refs"`
}

func (b *BGPRouter) AddRef(uuidTable *UUIDTableType, obj *ContrailConfig) {
	session_attributes := types.BgpSession{
		Attributes: []types.BgpSessionAttributes{{
			AdminDown:  false,
			Passive:    false,
			AsOverride: false,
			LoopCount:  0,
		}},
	}
	ref := Ref{
		UUID: obj.UUID,
		Type: obj.Type,
		Attr: map[string]interface{}{"attr": session_attributes},
	}
	switch obj.Type {
	case "bgp_router":
		b.BGPRouterRefs = append(b.BGPRouterRefs, ref)
	}
	b.UpdateDB(uuidTable)
}

func NewBGPRouter(fqNameTable *FQNameTableType, uuidTable *UUIDTableType, name, address, router_type string, port int) (*BGPRouter, error) {
	co, err := createContrailConfig(fqNameTable, "bgp_router", name, "routing_instance", []string{"default-domain", "default-project", "ip-fabric", "__default__", name})
	if err != nil {
		return nil, err
	}

	bgp_router_paramters := types.BgpRouterParams{
		AdminDown:        false,
		Vendor:           "contrail",
		AutonomousSystem: 64512,
		Identifier:       address,
		Address:          address,
		Port:             port,
		LocalAutonomousSystem: 64512,
		RouterType:            router_type,
		AddressFamilies: &types.AddressFamilies{
			Family: []string{"route-target", "inet-vpn", "e-vpn", "erm-vpn", "inet6-vpn"},
		},
	}
	b := &BGPRouter{
		ContrailConfig:      co,
		BGPRouterParameters: bgp_router_paramters,
	}

	if err := b.UpdateDB(uuidTable); err != nil {
		return nil, err
	}
	return b, nil
}

func (b *BGPRouter) UpdateDB(uuidTable *UUIDTableType) error {
	bytes, err := json.Marshal(b)
	if err != nil {
		return err
	}
	(*uuidTable)[b.UUID], err = b.Map(bytes)
	return err
}
