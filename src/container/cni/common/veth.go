// vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4
//
// Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
//

/****************************************************************************
 * File responsible to create and configure veth interfaces
 ****************************************************************************/
package cniIntf

import (
    "fmt"
    "net"
    "io/ioutil"

    log "../logging"
    "github.com/containernetworking/cni/pkg/ip"
    "github.com/containernetworking/cni/pkg/ns"
    "github.com/vishvananda/netlink"
)

// Definition for veth interface
type VEth struct {
    CniIntf
    HostIfName string
    // Since HostIfName interface is in host-namespace, veth interface is
    // initially created in host-namespace with a temporary name. The temporary
    // interface is then moved to container-namespace with containerIfName
    TmpHostIfName string
}

// Update Proc settings
func (intf *VEth) updateProc() error {
    // Update ipv6 route advertisement
    procFile := "/proc/sys/net/ipv6/conf/" + intf.HostIfName + "/accept_ra"
    err := ioutil.WriteFile(procFile, []byte("0"), 0600)
    if err != nil {
        log.Errorf("Error Updating Proc file %s. " +
            "Error : %s", procFile, err)
        return err
    }
    return nil
}

// Remove veth interface
// Deletes the tap interface from host-os. It will automatically
// remove the corresponding interface from container namespace also
func (intf VEth) Delete() error {
    log.Infof("Deleting VEth interface %+v", intf)
    err := intf.DeleteByName(intf.HostIfName)

    if err != nil {
        log.Errorf("Error deleting interface")
        return err
    }

    log.Errorf("Deleted interface")
    return nil
}

// Create VEth with a temporary name in host-os namespace and then move
func (intf *VEth) ensureHostIntf(netns ns.NetNS) error {
    veth := &netlink.Veth{
        LinkAttrs: netlink.LinkAttrs{
            Name:  intf.HostIfName,
            Flags: net.FlagUp,
            MTU:   intf.mtu,
        },
        PeerName: intf.TmpHostIfName,
    }

    // Create the link
    netlink.LinkAdd(veth)

    // We should have both ends of veth in host-os now
    _, err := netlink.LinkByName(intf.HostIfName)
    if err != nil {
        log.Errorf("Error creating VEth interface. Interface %s not found."+
            "Peer %s", intf.HostIfName, intf.containerIfName)
        return err
    }

    tmpLink, err := netlink.LinkByName(intf.TmpHostIfName)
    if err != nil {
        log.Errorf("Error creating VEth interface. Temporary interface %s "+
            "not found. HostIfName %s", intf.TmpHostIfName, intf.HostIfName)
        return err
    }

    err = netlink.LinkSetNsFd(tmpLink, int(netns.Fd()))
    if err != nil {
        log.Errorf("Error moving temporary interface %s to namespace %s",
            intf.TmpHostIfName, intf.containerNamespace)
        return err
    }

    err = netns.Do(func(_ ns.NetNS) error {
        err := ip.RenameLink(intf.TmpHostIfName, intf.containerIfName)
        if err != nil {
            log.Errorf("Error renaming interface %s to %s in container %s",
                intf.TmpHostIfName, intf.containerIfName,
                intf.containerNamespace)
            return err
        }

        containerLink, err := netlink.LinkByName(intf.containerIfName)
        if err != nil {
            log.Errorf("Error creating VEth interface. Container interface %s"+
                "not found. Host interface %s", intf.containerIfName,
                intf.HostIfName)
            return err
        }

        if err = netlink.LinkSetUp(containerLink); err != nil {
            log.Errorf("Error setting link-up for interface %s. Error : %v",
                intf.containerIfName, err)
            return err
        }
        return nil
    })

    if err != nil {
        log.Errorf("Error creating VEth interface")
        return err
    }

    return nil
}

// Create veth interfaces. One end of interface will be inside container and
// another end in host-os network namespace
func (intf VEth) Create() error {
    log.Infof("Creating VEth interface %+v", intf)

    // Open the namespace
    netns, err := ns.GetNS(intf.containerNamespace)
    if err != nil {
        log.Errorf("Error opening namespace %q: %v",
            intf.containerNamespace, err)
        return err
    }
    defer netns.Close()

    // Check if interface already present inside container
    err = netns.Do(func(_ ns.NetNS) error {
        _, err := netlink.LinkByName(intf.containerIfName)
        if err == nil {
            // Interface already present. This is most likely an unexpected
            // case of 2 ADD messages for same POD-UID??? Fail creating the
            // new container
            log.Infof("Interface %s already present inside container",
                intf.containerIfName)
            return fmt.Errorf("Interface %s already present inside container",
                intf.containerIfName)
        }

        return err
    })

    if err == nil {
        return nil
    }

    // Interface not present inside container.
    // Create VEth with one end in host-os and another inside container
    err = intf.ensureHostIntf(netns)
    if err != nil {
        log.Errorf("Error creating VEth interface")
        return err
    }

    intf.updateProc()

    log.Infof("VEth interface created")
    return nil
}

func (intf VEth) GetHostIfName() string {
    return intf.HostIfName
}

func (intf VEth) Log() {
    log.Infof("%+v", intf)
}

// Make tap-interface name in host namespace. Name is based on container-id
func buildHostIfName(str string) string {
    return "tap" + str[:CNI_UUID_IFNAME_LEN]
}

// The tap-interface interface is initially created in host-os namespace. The
// peer interface in such case is a temporary name
func buildTmpHostIfName(str string) string {
    return "tmp" + str[:CNI_UUID_IFNAME_LEN]
}

func InitVEth(containerIfName, containerId, containerUuid,
    containerNamespace string, mtu int) VEth {
    intf := VEth{
        CniIntf: CniIntf{
            containerId:        containerId,
            containerUuid:      containerUuid,
            containerIfName:    containerIfName,
            containerNamespace: containerNamespace,
            mtu:                mtu,
        },
        HostIfName: "",
    }

    nameStr := intf.containerIfName + "-" + intf.containerUuid
    intf.HostIfName = buildHostIfName(nameStr)
    intf.TmpHostIfName = buildTmpHostIfName(nameStr)

    log.Infof("Initialized VEth interface %+v\n", intf)
    return intf
}
