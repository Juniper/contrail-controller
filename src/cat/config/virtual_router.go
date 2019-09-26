package config

import (
	"encoding/json"
)

// VirtualRouter represents a compute node that runs contrail-vouter-agent and connects to control-node over xmpp.
type VirtualRouter struct {
	*ContrailConfig
	VirtualRouterIpAddress   string `json:"prop:virtual_router_ip_address"`
	VirtualRouterDpdkEnabled bool   `json:"prop:virtual_router_dpdk_enabled"`
	VirtualMachineRefs       []Ref  `json:"virtual_machine_refs"`
}

func (vr *VirtualRouter) AddRef(uuidTable *UUIDTableType, obj *ContrailConfig) {
	ref := Ref{
		UUID: obj.UUID, Type: obj.Type, Attr: map[string]interface{}{"attr": ""},
	}
	switch obj.Type {
	case "virtual_machine":
		vr.VirtualMachineRefs = append(vr.VirtualMachineRefs, ref)
	}
	vr.UpdateDB(uuidTable)
}

func NewVirtualRouter(fqNameTable *FQNameTableType, uuidTable *UUIDTableType, name, ip string) (*VirtualRouter, error) {
	co, err := createContrailConfig(fqNameTable, "virtual_router", name, "global-system-config", []string{"default-global-system-config", name})
	if err != nil {
		return nil, err
	}
	vr := &VirtualRouter{
		ContrailConfig:           co,
		VirtualRouterIpAddress:   ip,
		VirtualRouterDpdkEnabled: false,
	}

	err = vr.UpdateDB(uuidTable)
	if err != nil {
		return nil, err
	}
	return vr, nil
}

func (vr *VirtualRouter) UpdateDB(uuidTable *UUIDTableType) error {
	b, err := json.Marshal(vr)
	if err != nil {
		return err
	}
	(*uuidTable)[vr.UUID], err = vr.Map(b)
	return err
}
