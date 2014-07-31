/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_ksync_linux_bridge_h
#define vnsw_agent_ksync_linux_bridge_h

/**************************************************************************
 * Implements KSyncVxlanBridgeObject for Linux
 **************************************************************************/
class KSyncLinuxBridgeObject : public KSyncVxlanBridgeObject {
public:
    KSyncLinuxBridgeObject(KSyncLinuxVxlan *ksync);
    virtual ~KSyncLinuxBridgeObject() { }

    // Allocates an entry of type KSyncLinuxBridgeEntry from DBEntry of type
    // VxLanId
    KSyncEntry *DBToKSyncEntry(const DBEntry *e);
    // Allocate and initialize an entry of type KSyncLinuxBridgeEntry
    KSyncEntry *Alloc(const KSyncEntry *entry, uint32_t index);
private:
    DISALLOW_COPY_AND_ASSIGN(KSyncLinuxBridgeObject);
};

/**************************************************************************
 * Implements KSyncVxlanBridgeEntry for Linux
 **************************************************************************/
class KSyncLinuxBridgeEntry : public KSyncVxlanBridgeEntry {
public:
    KSyncLinuxBridgeEntry(KSyncLinuxBridgeObject *obj, const VxLanId *vxlan);
    KSyncLinuxBridgeEntry(KSyncLinuxBridgeObject *obj,
                          const KSyncLinuxBridgeEntry *entry);
    virtual ~KSyncLinuxBridgeEntry() { }

    const std::string name() const { return name_; }
    const std::string vxlan_port_name() const { return vxlan_port_name_; }

    // Creates a VxLanBridge and a Vxlan-Port
    bool Add();
    // Deletes the Vxlan Port and Vxlan Bridge
    bool Delete();
private:
    // Name of the Linux Vxlan Bridge created
    std::string name_;
    // Name of the Linux Vxlan port created
    std::string vxlan_port_name_;
    DISALLOW_COPY_AND_ASSIGN(KSyncLinuxBridgeEntry);
};
#endif // vnsw_agent_ksync_linux_bridge_h
