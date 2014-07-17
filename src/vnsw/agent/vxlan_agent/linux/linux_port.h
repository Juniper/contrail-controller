/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_ksync_linux_port_h
#define vnsw_agent_ksync_linux_port_h

class KSyncLinuxPortEntry : public KSyncVxlanPortEntry {
public:
    KSyncLinuxPortEntry(KSyncLinuxPortObject *obj, const Interface *interface);
    KSyncLinuxPortEntry(KSyncVxlanPortObject *obj,
                        const KSyncLinuxPortEntry *entry);
    virtual ~KSyncLinuxPortEntry() { }

    bool Add();
    bool Change();
    bool Delete();
private:
    const KSyncLinuxBridgeEntry *old_bridge_;
    DISALLOW_COPY_AND_ASSIGN(KSyncLinuxPortEntry);
};

class KSyncLinuxPortObject : public KSyncVxlanPortObject {
public:
    KSyncLinuxPortObject(KSyncLinuxVxlan *ksync);
    virtual ~KSyncLinuxPortObject() { }

    virtual KSyncEntry *Alloc(const KSyncEntry *entry, uint32_t index);
    virtual KSyncEntry *DBToKSyncEntry(const DBEntry *e);

private:
    DISALLOW_COPY_AND_ASSIGN(KSyncLinuxPortObject);
};

#endif // vnsw_agent_ksync_linux_port_h
