// vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4
//
// Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
//

/****************************************************************************
 * File responsible to create and configure veth interfaces
 ****************************************************************************/
package cniVEth

import (
	"fmt"
	"github.com/containernetworking/cni/pkg/ip"
	"github.com/containernetworking/cni/pkg/ipam"
	"github.com/containernetworking/cni/pkg/ns"
	"github.com/containernetworking/cni/pkg/types/current"
	"github.com/golang/glog"
	"github.com/vishvananda/netlink"
	"net"
)

// Number of characters to pick from UUID for ifname
const CNI_UUID_IFNAME_LEN = 11

// Default MTU for interface configured
const CNI_MTU = 1500

type VEth struct {
	containerIfName    string
	containerNamespace string
	containerUuid      string
	HostIfName         string
	mtu                int
}

// Make tap-interface name in host namespace. Name is based on UUID
func buildIfName(uuid string) string {
	return "tap" + uuid[:CNI_UUID_IFNAME_LEN]
}

// Remove veth interface
// Deletes the interface from container namespace. It will automatically
// remove the corresponding interface from host-namespace also
func (veth *VEth) Delete() error {
	// Enter container namespace and remove interface inside it
	err := ns.WithNetNSPath(veth.containerNamespace, func(_ ns.NetNS) error {
		// Get the link
		iface, err := netlink.LinkByName(veth.containerIfName)
		if err != nil {
			// Link already deleted. Nothing else to do
			glog.V(2).Infof("Interface %s not present inside container namespace %s. %+v",
				veth.containerIfName, veth.containerNamespace, err)
			return nil
		}

		// Delete link inside container
		if err = netlink.LinkDel(iface); err != nil {
			glog.V(2).Infof("Error deleting interface %s inside container",
				veth.containerIfName)
			return fmt.Errorf("Error deleting interface %s: %v",
				veth.containerIfName, err)
		}

		return nil
	})

	return err
}

// Configure veth interface
// Interface inside container is already created. Configure MAC address,
// IP address etc on the interface
func (veth *VEth) Configure(mac string, result *current.Result) error {
	// Validate MAC address
	hwAddr, mac_err := net.ParseMAC(mac)
	if mac_err != nil {
		glog.Errorf("Error parsing MAC address ", mac, " Error ", mac_err)
		return fmt.Errorf("Error parsing MAC address ", mac,
			" Error ", mac_err)
	}

	// Configure interface inside container
	err := ns.WithNetNSPath(veth.containerNamespace, func(_ ns.NetNS) error {
		// Update MAC address for the interface
		iface, err := netlink.LinkByName(veth.containerIfName)
		if err != nil {
			glog.Errorf("Failed to lookup %q: %v", veth.containerIfName, err)
			return fmt.Errorf("Failed to lookup %q: %v", veth.containerIfName,
				err)
		}

		if err = netlink.LinkSetHardwareAddr(iface, hwAddr); err != nil {
			glog.Errorf("Failed to set hardware addr %s to %q: %v",
				hwAddr, veth.containerIfName, err)
			return fmt.Errorf("Failed to set hardware addr %s to %q: %v",
				hwAddr, veth.containerIfName, err)
		}

		// Configure IPAM attributes
		err = ipam.ConfigureIface(veth.containerIfName, result)
		if err != nil {
			glog.Errorf("Error configuring interface %s with %s. %+v",
				veth.containerIfName, result, err)
			fmt.Errorf("Error configuring interface %s with %s. %+v",
				veth.containerIfName, result, err)
			return err
		}

		glog.Infof("Configured interface %s with %s", veth.containerIfName,
			result)
		return nil
	})

	return err
}

// Create veth interfaces. One end of interface will be inside container and
// another end in host-os network namespace
func (veth *VEth) Create() error {
	// The veth is created with a temporary name in container namespace.
	// The temporary name is passed in this variable
	var hostIfNameTmp string

	// create veth pair in container and move host end into host netns
	err := ns.WithNetNSPath(veth.containerNamespace,
		func(hostNS ns.NetNS) error {
			// Interface already present?
			_, err := netlink.LinkByName(veth.containerIfName)
			if err == nil {
				glog.V(2).Infof("Interface %s already present inside container",
					veth.containerIfName)
				return nil
			}

			// Create the interfaces
			link, _, err := ip.SetupVeth(veth.containerIfName,
				veth.mtu, hostNS)
			if err != nil {
				glog.V(2).Infof("Error creating VEth interface %s. %+v",
					veth.containerIfName, err)
				return err
			}

			// Get name of other end of interface
			hostIfNameTmp = link.Attrs().Name

			glog.V(2).Infof("Created VEth interfaces %s and %s",
				veth.containerIfName, hostIfNameTmp)
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

		err = ip.RenameLink(hostIfNameTmp, veth.HostIfName)
		if err != nil {
			glog.Errorf("Error renaming %s to %s. %+v",
				hostIfNameTmp, veth.HostIfName, err)
		}

		err = netlink.LinkSetUp(link)
		if err != nil {
			glog.Errorf("Error setting link up for %s. %+v",
				veth.HostIfName, err)
			return err
		}
	}

	return err
}

func (veth *VEth) Log() {
	glog.V(2).Infof("%+v", *veth)
}

func Init(containerIfName, containerNamespace, containerUuid string) *VEth {
	veth := VEth{containerIfName: containerIfName,
		containerNamespace: containerNamespace, containerUuid: containerUuid,
		HostIfName: "", mtu: CNI_MTU}
	veth.HostIfName = buildIfName(veth.containerUuid)
	return &veth
}
