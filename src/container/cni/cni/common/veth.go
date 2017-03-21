// vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4
//
// Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
//

/****************************************************************************
 * File responsible to create and configure veth interfaces
 ****************************************************************************/
package cniIntf

import (
	"github.com/containernetworking/cni/pkg/ip"
	"github.com/containernetworking/cni/pkg/ns"
	"github.com/golang/glog"
	"github.com/vishvananda/netlink"
)

// Definition for veth interface
type VEth struct {
	CniIntf
	HostIfName string
}

// Remove veth interface
// Deletes the tap interface from host-os. It will automatically
// remove the corresponding interface from container namespace also
func (intf VEth) Delete() error {
	return intf.DeleteByName(intf.HostIfName)
}

// Create veth interfaces. One end of interface will be inside container and
// another end in host-os network namespace
func (intf VEth) Create() error {
	// The veth is created with a temporary name in container namespace.
	// The temporary name is passed in this variable
	var hostIfNameTmp string

	// create veth pair in container and move host end into host netns
	err := ns.WithNetNSPath(intf.containerNamespace,
		func(hostNS ns.NetNS) error {
			// Interface already present?
			_, err := netlink.LinkByName(intf.containerIfName)
			if err == nil {
				glog.V(2).Infof("Interface %s already present inside container",
					intf.containerIfName)
				return nil
			}

			// Create the interfaces
			link, _, err := ip.SetupVeth(intf.containerIfName,
				intf.mtu, hostNS)
			if err != nil {
				glog.V(2).Infof("Error creating VEth interface %s. %+v",
					intf.containerIfName, err)
				return err
			}

			// Get name of other end of interface
			hostIfNameTmp = link.Name

			glog.V(2).Infof("Created VEth interfaces %s and %s",
				intf.containerIfName, hostIfNameTmp)
			return nil
		})

	// Rename the host-os part of interface. Will need to bring down interface
	// before renaming
	if len(hostIfNameTmp) > 0 {
		link, err := netlink.LinkByName(hostIfNameTmp)
		if err != nil {
			glog.Errorf("Error getting link down for %s. %+v",
				hostIfNameTmp, err)
			return err
		}

		err = netlink.LinkSetDown(link)
		if err != nil {
			glog.Errorf("Error setting link down for %s. %+v",
				hostIfNameTmp, err)
			return err
		}

		err = ip.RenameLink(hostIfNameTmp, intf.HostIfName)
		if err != nil {
			glog.Errorf("Error renaming %s to %s. %+v",
				hostIfNameTmp, intf.HostIfName, err)
		}

		err = netlink.LinkSetUp(link)
		if err != nil {
			glog.Errorf("Error setting link up for %s. %+v",
				intf.HostIfName, err)
			return err
		}
	}

	return err
}

func (intf VEth) GetHostIfName() string {
	return intf.HostIfName
}

func (intf VEth) Log() {
	glog.V(2).Infof("%+v", intf)
}

// Make tap-interface name in host namespace. Name is based on UUID
func buildIfName(uuid string) string {
	return "tap" + uuid[:CNI_UUID_IFNAME_LEN]
}

func InitVEth(containerIfName, containerUuid, containerNamespace string) VEth {
	intf := VEth{
		CniIntf: CniIntf{
			containerUuid:      containerUuid,
			containerIfName:    containerIfName,
			containerNamespace: containerNamespace,
			mtu:                CNI_MTU,
		},
		HostIfName: "",
	}

	intf.HostIfName = buildIfName(intf.containerUuid)
	return intf
}
