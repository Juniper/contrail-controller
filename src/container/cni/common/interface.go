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
    "net"

    log "../logging"
    "github.com/containernetworking/cni/pkg/ipam"
    "github.com/containernetworking/cni/pkg/ns"
    "github.com/containernetworking/cni/pkg/types/current"
    "github.com/vishvananda/netlink"
)

// Number of characters to pick from UUID for ifname
const CNI_UUID_IFNAME_LEN = 11

// Number of characters from start to pick from container-id for ifname
const CNI_ID_IFNAME_START_LEN = 5

// Number of characters from end to pick from container-id for ifname
const CNI_ID_IFNAME_END_LEN = 5

// Default MTU for interface configured
const CNI_MTU = 1500

// Prefix for macvlan interface alias
const INTF_ALIAS = "contrail-k8s-cni-vlan-"

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
    containerId        string
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
    log.Infof("Deleting interface %s", ifName)
    // Get link for the interface
    link, err := netlink.LinkByName(ifName)
    if err != nil {
        log.Infof("Interface %s not present. Error %+v", ifName, err)
        return nil
    }

    // Delete the link
    if err = netlink.LinkDel(link); err != nil {
        msg := fmt.Sprintf("Error deleting interface %s. Error %+v", ifName,
            err)
        log.Errorf(msg)
        return fmt.Errorf(msg)
    }

    log.Infof("Deleted interface %s", ifName)
    return nil
}

// DeleteByAlias deletes an interface with the given link alias
func (intf CniIntf) DeleteByAlias(intfAlias string) error {
    log.Infof("Delete interface with alias %s", intfAlias)
    // Get link for the interface
    link, err := netlink.LinkByAlias(intfAlias)
    if err != nil {
        log.Infof("Interface with alias %s is not present. Error %+v",
            intfAlias, err)
        return nil
    }

    log.Infof("Found link %+v with alias %s", link, intfAlias)

    // Delete the link
    if err = netlink.LinkDel(link); err != nil {
        msg := fmt.Sprintf("Error deleting interface with alias %s. Error %+v",
            intfAlias, err)
        log.Errorf(msg)
        return fmt.Errorf(msg)
    }

    log.Infof("Deleted interface with alias %s", intfAlias)
    return nil
}

// Configure MAC address and IP address on the interface
// Assumes that interface inside container is already created.
func (intf CniIntf) Configure(mac string, result *current.Result) error {
    log.Infof("Configuring interface %s with mac %s and %+v",
        intf.containerIfName, mac, result)
    hwAddr, err := net.ParseMAC(mac)
    if err != nil {
        msg := fmt.Sprintf("Error parsing MAC address %s. Error %+v", mac, err)
        log.Errorf(msg)
        return fmt.Errorf(msg)
    }

    // Configure interface inside container
    err = ns.WithNetNSPath(intf.containerNamespace, func(_ ns.NetNS) error {
        // Find the link first
        link, err := netlink.LinkByName(intf.containerIfName)
        if err != nil {
            msg := fmt.Sprintf("Failed to lookup interface %q: Error %+v",
                intf.containerIfName, err)
            log.Errorf(msg)
            return fmt.Errorf(msg)
        }

        // Update MAC address for the interface
        if err = netlink.LinkSetHardwareAddr(link, hwAddr); err != nil {
            msg := fmt.Sprintf("Failed to set hardware addr %s to %q: Error %v",
                hwAddr, intf.containerIfName, err)
            log.Errorf(msg)
            return fmt.Errorf(msg)
        }

        // Configure IPAM attributes
        err = ipam.ConfigureIface(intf.containerIfName, result)
        if err != nil {
            msg := fmt.Sprintf("Error configuring interface %s with %s. "+
                "Error %+v", intf.containerIfName, result, err)
            log.Errorf(msg)
            return fmt.Errorf(msg)
        }

        log.Infof("Configure successful")
        return nil
    })

    return err
}
