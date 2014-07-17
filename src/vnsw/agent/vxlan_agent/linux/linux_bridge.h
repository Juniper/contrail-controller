/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_ksync_linux_bridge_h
#define vnsw_agent_ksync_linux_bridge_h

class KSyncLinuxBridgeEntry : public KSyncVxlanBridgeEntry {
public:
    KSyncLinuxBridgeEntry(KSyncLinuxBridgeObject *obj, const VxLanId *vxlan);
    KSyncLinuxBridgeEntry(KSyncLinuxBridgeObject *obj,
                          const KSyncLinuxBridgeEntry *entry);
    virtual ~KSyncLinuxBridgeEntry() { }

    const std::string name() const { return name_; }
    const std::string vxlan_port_name() const { return vxlan_port_name_; }

    bool Add();
    bool Change();
    bool Delete();
private:
    std::string name_;
    std::string vxlan_port_name_;
    DISALLOW_COPY_AND_ASSIGN(KSyncLinuxBridgeEntry);
};

class KSyncLinuxBridgeObject : public KSyncVxlanBridgeObject {
public:
    KSyncLinuxBridgeObject(KSyncLinuxVxlan *ksync);
    virtual ~KSyncLinuxBridgeObject() { }

    KSyncEntry *Alloc(const KSyncEntry *entry, uint32_t index);
    KSyncEntry *DBToKSyncEntry(const DBEntry *e);
private:
    DISALLOW_COPY_AND_ASSIGN(KSyncLinuxBridgeObject);
};

#endif // vnsw_agent_ksync_linux_bridge_h
