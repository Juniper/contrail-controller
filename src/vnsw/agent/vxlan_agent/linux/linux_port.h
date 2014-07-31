/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_ksync_linux_port_h
#define vnsw_agent_ksync_linux_port_h

/**************************************************************************
 * Implements KSyncVxlanPortObject for Linux
 **************************************************************************/
class KSyncLinuxPortObject : public KSyncVxlanPortObject {
public:
    KSyncLinuxPortObject(KSyncLinuxVxlan *ksync);
    virtual ~KSyncLinuxPortObject() { }

    // Allocates an entry of type KSyncLinuxPortEntry from DBEntry of type
    // Interface
    virtual KSyncEntry *DBToKSyncEntry(const DBEntry *e);

    // Allocate and initialize an entry of type KSyncLinuxPortEntry
    virtual KSyncEntry *Alloc(const KSyncEntry *entry, uint32_t index);

private:
    DISALLOW_COPY_AND_ASSIGN(KSyncLinuxPortObject);
};

/**************************************************************************
 * Implements KSyncVxlanPortEntry for Linux
 **************************************************************************/
class KSyncLinuxPortEntry : public KSyncVxlanPortEntry {
public:
    KSyncLinuxPortEntry(KSyncLinuxPortObject *obj, const Interface *interface);
    KSyncLinuxPortEntry(KSyncVxlanPortObject *obj,
                        const KSyncLinuxPortEntry *entry);
    virtual ~KSyncLinuxPortEntry() { }

    // Adds port to the bridge
    bool Add();
    // Handle change to the port. If there is change in bridge, removes port
    // from old bridge and adds to new bridge
    bool Change();
    // Deletes port from the bridge
    bool Delete();
private:
    const KSyncLinuxBridgeEntry *old_bridge_;
    DISALLOW_COPY_AND_ASSIGN(KSyncLinuxPortEntry);
};
#endif // vnsw_agent_ksync_linux_port_h
