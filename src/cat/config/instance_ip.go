package config

import (
	"encoding/json"
)

// InstanceIp represents IP address of an instance on a particular interface.
type InstanceIp struct {
	*ContrailConfig
	InstanceIpAddress           string `json:"prop:instance_ip_address"`
	InstanceIpFamily            string `json:"prop:instance_ip_family"`
	VirtualNetworkRefs          []Ref  `json:"virtual_network_refs"`
	VirtualMachineInterfaceRefs []Ref  `json:"virtual_machine_interface_refs"`
}

func (i *InstanceIp) AddRef(uuidTable *UUIDTableType, obj *ContrailConfig) {
	ref := Ref{
		UUID: obj.UUID, Type: obj.Type, Attr: map[string]interface{}{"attr": ""},
	}
	switch obj.Type {
	case "virtual_network":
		i.VirtualNetworkRefs = append(i.VirtualNetworkRefs, ref)
	case "virtual_machine_interface":
		i.VirtualMachineInterfaceRefs = append(i.VirtualMachineInterfaceRefs, ref)
	}
	i.UpdateDB(uuidTable)
}

func NewInstanceIp(fqNameTable *FQNameTableType, uuidTable *UUIDTableType, name, address, family string) (*InstanceIp, error) {
	ip, err := createContrailConfig(fqNameTable, "instance_ip", name, "", []string{name})
	i := &InstanceIp{
		ContrailConfig:    ip,
		InstanceIpAddress: address,
		InstanceIpFamily:  family,
	}

	err = i.UpdateDB(uuidTable)
	if err != nil {
		return nil, err
	}
	return i, nil
}

func (i *InstanceIp) UpdateDB(uuidTable *UUIDTableType) error {
	b, err := json.Marshal(i)
	if err != nil {
		return err
	}
	(*uuidTable)[i.UUID], err = i.Map(b)
	return err
}
