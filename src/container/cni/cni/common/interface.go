// vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4
//
// Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
//

/****************************************************************************
 * Base class for interface managed by Contrail Cni
 ****************************************************************************/
package cniIntf

import (
	"fmt"
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

// Common methods for all CniIntf
type CniIntfMethods interface {
	Log()
	Create() error
	Delete() error
	Configure(mac string, result *current.Result) error
	GetHostIfName() string
}

// Base definition for CniIntf
type CniIntf struct {
	containerUuid      string
	containerIfName    string
	containerNamespace string
	mtu                int
}

// Delete an interface with given name
// Every interface inside a container has a peer in host-namespace. Deleting
// the interface in host-namespace deletes interface in container also.
// The API is used to delete interface in host-namespace
func (intf CniIntf) DeleteByName(ifName string) error {
	// Get link for the interface
	link, err := netlink.LinkByName(ifName)
	if err != nil {
		glog.V(2).Infof("Interface %s not present. Error %+v", ifName, err)
		return nil
	}

	// Delete the link
	if err = netlink.LinkDel(link); err != nil {
		glog.Errorf("Error deleting interface %s. Error %+v", ifName, err)
		return fmt.Errorf("Error deleting interface %s. Error %+v", ifName, err)
	}

	glog.V(2).Infof("Deleted interface %s", ifName)
	return nil
}

// Configure MAC address and IP address on the interface
// Assumes that interface inside container is already created.
func (intf CniIntf) Configure(mac string, result *current.Result) error {
	hwAddr, err := net.ParseMAC(mac)
	if err != nil {
		glog.Errorf("Error parsing MAC address %s. Error %+v", mac, err)
		return fmt.Errorf("Error parsing MAC address ", mac,
			" Error ", err)
	}

	// Configure interface inside container
	err = ns.WithNetNSPath(intf.containerNamespace, func(_ ns.NetNS) error {
		// Find the link first
		link, err := netlink.LinkByName(intf.containerIfName)
		if err != nil {
			glog.Errorf("Failed to lookup interface %q: Error %+v",
				intf.containerIfName, err)
			return fmt.Errorf("Failed to lookup interface %q: Error %+v",
				intf.containerIfName, err)
		}

		// Update MAC address for the interface
		if err = netlink.LinkSetHardwareAddr(link, hwAddr); err != nil {
			glog.Errorf("Failed to set hardware addr %s to %q: Error %v",
				hwAddr, intf.containerIfName, err)
			return fmt.Errorf("Failed to set hardware addr %s to %q: Error %v",
				hwAddr, intf.containerIfName, err)
		}

		// Configure IPAM attributes
		err = ipam.ConfigureIface(intf.containerIfName, result)
		if err != nil {
			glog.Errorf("Error configuring interface %s with %s. Error %+v",
				intf.containerIfName, result, err)
			fmt.Errorf("Error configuring interface %s with %s. Error %+v",
				intf.containerIfName, result, err)
			return err
		}

		glog.Infof("Configured interface %s with mac %s and %+v",
			intf.containerIfName, mac, result)
		return nil
	})

	return err
}
