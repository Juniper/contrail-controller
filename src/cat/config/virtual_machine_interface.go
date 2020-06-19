package config

import (
	"encoding/json"
)

// VirtualMachineInterface represents an interface (aka port) of a virtual-machine/container/bms
type VirtualMachineInterface struct {
	*ContrailConfig
	VirtualMachineRefs  []Ref `json:"virtual_machine_refs"`
	VirtualNetworkRefs  []Ref `json:"virtual_network_refs"`
	RoutingInstanceRefs []Ref `json:"routing_instance_refs"`
}

func (vmi *VirtualMachineInterface) AddRef(uuidTable *UUIDTableType, obj *ContrailConfig) {
	switch obj.Type {
	case "virtual_machine":
		ref := Ref{
			UUID: obj.UUID, Type: obj.Type,
			Attr: map[string]interface{}{"attr": ""},
		}
		vmi.VirtualMachineRefs = append(vmi.VirtualMachineRefs, ref)
	case "virtual_network":
		ref := Ref{
			UUID: obj.UUID, Type: obj.Type,
			Attr: map[string]interface{}{"attr": ""},
		}
		vmi.VirtualNetworkRefs = append(vmi.VirtualNetworkRefs, ref)
	case "routing_instance":
		ref := Ref{
			UUID: obj.UUID, Type: obj.Type,
			Attr: map[string]interface{}{"attr": map[string]string{"direction": "both"}, "is_weakref": false},
		}
		vmi.RoutingInstanceRefs = append(vmi.RoutingInstanceRefs, ref)
	}
	vmi.UpdateDB(uuidTable)
}

func NewVirtualMachineInterface(fqNameTable *FQNameTableType, uuidTable *UUIDTableType, name, project_uuid string) (*VirtualMachineInterface, error) {
	co, err := createContrailConfig(fqNameTable, "virtual_machine_interface", name, "project:"+project_uuid, []string{"default-domain", "default-project", name})
	if err != nil {
		return nil, err
	}
	vmi := &VirtualMachineInterface{
		ContrailConfig: co,
	}
	err = vmi.UpdateDB(uuidTable)
	if err != nil {
		return nil, err
	}
	return vmi, nil
}

func (vmi *VirtualMachineInterface) UpdateDB(uuidTable *UUIDTableType) error {
	b, err := json.Marshal(vmi)
	if err != nil {
		return err
	}
	(*uuidTable)[vmi.UUID], err = vmi.Map(b)
	return err
}
