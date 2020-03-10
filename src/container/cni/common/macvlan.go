// vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4
//
// Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
//

/****************************************************************************
 * File responsible to create and configure macvlan interfaces
 * This is typically used in nested-k8s scenario where containers are spawned
 * inside the container. The VMI for container is modeled as sub-interface in
 * this case.

 * The class creates a vlan-interface corresponding to the vlan in
 * sub-interface and then creates a macvlan interface over it.
 ****************************************************************************/
package cniIntf

import (
    "fmt"
    "strconv"

    log "../logging"
    "github.com/containernetworking/cni/pkg/ip"
    "github.com/containernetworking/cni/pkg/ns"
    "github.com/vishvananda/netlink"
)

// Definition for MacVlan interfaces
type MacVlan struct {
    CniIntf
    // parent interface on which vlan-interface is created
    ParentIfName string
    // vlan interface name
    vlanIfName string
    // tag for the vlan-interface
    vlanId int
    // Since vlan-interface is in host-namespace, the macvlan is initially
    // created in host-namespace with a temporary name. The temporary interface
    // is then moved to container-namespace with containerIfName
    containerTmpIfName string
}

// Remove macvlan interface
// Deletes the vlan interface from host-os. It will automatically
// remove the corresponding interface from container namespace also
func (intf MacVlan) Delete() error {
    return intf.DeleteByName(intf.vlanIfName)
}

// Create Vlan interface if not already present
// Ensure oper-state for interface is up
func (intf *MacVlan) ensureVlanIntf() (*netlink.Link, error) {
    log.Infof("Creating vlan interface %+v", *intf)

    // Check if VLAN Interface already present
    _, err := netlink.LinkByName(intf.vlanIfName)
    if err == nil {
        // Interface already present. This is most likely an unexpected
        // case of 2 ADD messages for same POD-UID??? Fail creating the
        // new container
        log.Infof("vlan interface %s already present", intf.vlanIfName)
        return nil, fmt.Errorf("Error in bring up of vlan interface %s. "+
            "Error %+v", intf.vlanIfName, err)
    }

    // Vlan Interface not present. Create it
    // First ensure parent interface is found
    parentIntf, err := netlink.LinkByName(intf.ParentIfName)
    if err != nil {
        log.Errorf("Error in getting parent interface %s. Error %+v",
            intf.ParentIfName, err)
        return nil, err
    }

    // Create Vlan Interface
    vlanIntf := &netlink.Vlan{
        netlink.LinkAttrs{
            MTU:         intf.mtu,
            Name:        intf.vlanIfName,
            ParentIndex: parentIntf.Attrs().Index,
        },
        intf.vlanId,
    }
    if err := netlink.LinkAdd(vlanIntf); err != nil {
        log.Errorf("Error creating vlan interface %s vlan-id %d"+
            "parent-intf %s. Error : %+v", intf.vlanIfName, intf.vlanId,
            intf.ParentIfName, err)
        return nil, err
    }

    // Query newly created Vlan Interface
    vlanLink, err := netlink.LinkByName(intf.vlanIfName)
    if err != nil {
        log.Errorf("Error querying vlan interface %s. Error %+v",
            intf.vlanIfName, err)
        return nil, err
    }

    log.Infof("Created new link -  %+v", vlanLink)

    // Create and set alias for the link with the given vlan id.
    // The alias is used to identify un-freed resources if a DEL
    // call is missed on pod deletion.
    interfaceAlias := buildLinkAlias(intf.vlanId)
    err = netlink.LinkSetAlias(vlanLink, interfaceAlias)
    if err != nil {
        log.Errorf("Error setting link alias %s. Error %+v",
            interfaceAlias, err)
        return nil, err
    }

    log.Infof("Set alias - %s for the new link -  %+v", interfaceAlias, vlanLink)

    // Bringup oper-state of Vlan Interface
    err = netlink.LinkSetUp(vlanLink)
    if err != nil {
        log.Errorf("Error setting oper-state of vlan interface %s. Error %+v",
            intf.vlanIfName, err)
        return nil, err
    }

    log.Infof("Created vlan interface %+v", *intf)
    return &vlanLink, nil
}

// The mac-vlan is created in host-namespace with a temporary name.
// Rename the temporary interface to containerIfName
func rename(link netlink.Link, ifName, containerIfName string) (netlink.Link,
    error) {
    log.Infof("Renaming interface %s to %s.", ifName, containerIfName)
    // Bring down interface before renaming
    err := netlink.LinkSetDown(link)
    if err != nil {
        log.Errorf("Error bringing interface down : %+v. Error %+v",
            ifName, err)
        return nil, err
    }

    // Rename temporary interface
    err = netlink.LinkSetName(link, containerIfName)
    if err != nil {
        log.Errorf("Error renaming interface %s to %s : %+v", ifName,
            containerIfName, err)
        return nil, err
    }

    // Bring interface up
    err = netlink.LinkSetUp(link)
    if err != nil {
        log.Errorf("Error bringing interface up : %+v. Error %+v",
            containerIfName, err)
        return nil, err
    }

    log.Infof("Rename successful")
    return netlink.LinkByName(containerIfName)
}

// Create the mac-vlan interface
func (intf *MacVlan) ensureMacVlanIntf(vlanLink netlink.Link) error {
    log.Infof("Creating MacVlan interface %s in namespace %s",
        intf.containerIfName, intf.containerNamespace)
    var cn_link *netlink.Link = nil
    err := ns.WithNetNSPath(intf.containerNamespace,
        func(hostNS ns.NetNS) error {
            // Check if macvlan interface present inside container already
            link, err := netlink.LinkByName(intf.containerIfName)
            if err == nil {
                log.Infof("Interface %s already present inside container",
                    intf.containerIfName)
                cn_link = &link
                return nil
            }

            // containerIfName not present. Check if the temporary interface
            // already present
            link, err = netlink.LinkByName(intf.containerTmpIfName)
            if err != nil {
                // Temporary interface also not present
                log.Infof("Temporary interface %s not present inside ",
                    "container", intf.containerIfName, intf.containerNamespace)
                return nil
            }

            // temporary interface present. Rename it
            link, err = rename(link, intf.containerTmpIfName,
                intf.containerIfName)
            if err == nil {
                log.Errorf("Error renaming interface %s to %s. Error : %+v",
                    intf.containerTmpIfName, intf.containerIfName, err)
                return err
            }

            cn_link = &link
            return nil
        })

    // Nothing to do if the containerIfName is already present
    if cn_link != nil {
        fmt.Printf("Interface %s present. Nothing to do\n",
            intf.containerIfName)
        return nil
    }

    if err != nil {
        log.Errorf("Failed creating MacVlan interface")
        return err
    }

    netns, err := ns.GetNS(intf.containerNamespace)
    if err != nil {
        log.Errorf("Error opening namespace %q: %v",
            intf.containerNamespace, err)
        return err
    }
    defer netns.Close()

    macvlan := &netlink.Macvlan{
        LinkAttrs: netlink.LinkAttrs{
            MTU:         intf.mtu,
            Name:        intf.containerTmpIfName,
            ParentIndex: vlanLink.Attrs().Index,
            Namespace:   netlink.NsFd(int(netns.Fd())),
        },
        Mode: netlink.MACVLAN_MODE_VEPA,
    }
    if err := netlink.LinkAdd(macvlan); err != nil {
        log.Errorf("Error creating mac-vlan interface %q: %v",
            intf.containerTmpIfName, err)
        return err
    }

    // Temporary interface created. Move to containerNamespace and rename it
    return netns.Do(func(_ ns.NetNS) error {
        err := ip.RenameLink(intf.containerTmpIfName, intf.containerIfName)
        if err != nil {
            _ = netlink.LinkDel(macvlan)
            log.Errorf("Error renaming macvlan to %q: %v",
                intf.containerIfName, err)
            return err
        }

        // Re-fetch macvlan to get all properties/attributes
        _, err = netlink.LinkByName(intf.containerIfName)
        if err != nil {
            log.Errorf("Error in fetching macvlan %q: %v",
                intf.containerIfName, err)
            return err
        }

        return nil
    })
}

// Create MacVlan interfaces.
func (intf MacVlan) Create() error {
    log.Infof("Creating MacVlan interface %+v", intf)

    // Check and delete any links existing with the given vlan-id.
    interfaceAlias := buildLinkAlias(intf.vlanId)
    err := intf.DeleteByAlias(interfaceAlias)
    if err != nil {
        log.Errorf("Could not delete existing link with alias %s. Error %+v",
            interfaceAlias, err)
        return err
    }

    // Ensure Vlan Interface is created
    vlanLink, err := intf.ensureVlanIntf()
    if err != nil {
        log.Errorf("Error creating vlan interface %+v. Error %+v", intf, err)
        return err
    }

    // Ensure mac-vlan interface is created
    err = intf.ensureMacVlanIntf(*vlanLink)
    if err != nil {
        log.Errorf("Error creating mac-vlan interface %+v. Error %+v",
            intf, err)
        return err
    }

    log.Infof("MacVlan interface created")
    return nil
}

func buildLinkAlias(vlanId int) string {
    return INTF_ALIAS + strconv.Itoa(vlanId)
}

func buildVlanIfName(str string, vlanId int) string {
    return "vlan" + str[:CNI_UUID_IFNAME_LEN]
}

func buildContainerTmpIfName(str string) string {
    return "mac" + str[:CNI_UUID_IFNAME_LEN]
}

func (intf MacVlan) GetHostIfName() string {
    return intf.ParentIfName
}

func (intf MacVlan) Log() {
    log.Infof("%+v", intf)
}

// Create MacVlan interface object
func InitMacVlan(parentIfName, containerIfName, containerId, containerUuid,
    containerNamespace string, mtu, vlanId int) MacVlan {

    intf := MacVlan{
        CniIntf: CniIntf{
            containerId:        containerId,
            containerUuid:      containerUuid,
            containerIfName:    containerIfName,
            containerNamespace: containerNamespace,
            mtu:                mtu,
        },
        ParentIfName:       parentIfName,
        vlanIfName:         "",
        vlanId:             vlanId,
        containerTmpIfName: "",
    }

    nameStr := intf.containerIfName + "-" + intf.containerUuid
    intf.vlanIfName = buildVlanIfName(nameStr, intf.vlanId)
    intf.containerTmpIfName = buildContainerTmpIfName(nameStr)

    log.Infof("Initialized MacVlan interface %+v\n", intf)
    return intf
}
