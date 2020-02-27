package models

import (
	"strconv"

	"github.com/Juniper/asf/pkg/errutil"
	"github.com/Juniper/asf/pkg/models/basemodels"
)

const (
	noneString       = "None"
	internalVNPrefix = "__contrail_lr_internal_vn_"
)

// GetVXLanIDInLogicaRouter returns vxlan network identifier property
func (lr *LogicalRouter) GetVXLanIDInLogicaRouter() string {
	id := lr.GetVxlanNetworkIdentifier()
	if id == noneString || id == "" {
		return ""
	}

	return id
}

// GetInternalVNName returns proper internal virtual network name
func (lr *LogicalRouter) GetInternalVNName() string {
	return internalVNPrefix + lr.GetUUID() + "__"
}

// GetInternalVNFQName returns internal virtual network fqName
func (lr *LogicalRouter) GetInternalVNFQName(parentProject *Project) []string {
	return basemodels.ChildFQName(parentProject.GetFQName(), lr.GetInternalVNName())
}

// ConvertVXLanIDToInt converts vxlan network id form string to int
func (lr *LogicalRouter) ConvertVXLanIDToInt() (int64, error) {
	vxlanNetworkID := lr.GetVxlanNetworkIdentifier()
	id, err := strconv.ParseInt(vxlanNetworkID, 10, 64)
	if err != nil {
		return 0, errutil.ErrorBadRequestf("vxlan network id must be a number(%s): %v", vxlanNetworkID, err)
	}

	return id, nil
}

// RemoveVirtualMachineInterfaceRefWithID removes VMI refs from logical router using its ID.
func (lr *LogicalRouter) RemoveVirtualMachineInterfaceRefWithID(uuid string) {
	for i, ref := range lr.GetVirtualMachineInterfaceRefs() {
		if uuid == ref.UUID {
			lr.VirtualMachineInterfaceRefs = append(lr.VirtualMachineInterfaceRefs[:i],
				lr.VirtualMachineInterfaceRefs[i+1:]...)
		}
	}
}
