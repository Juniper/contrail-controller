/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_linux_vxlan_h
#define vnsw_agent_linux_vxlan_h

class KSyncLinuxBridgeObject;
class KSyncLinuxBridgeEntry;
class KSyncLinuxPortObject;
class KSyncLinuxPortEntry;
class KSyncLinuxFdbObject;
class KSyncLinuxFdbEntry;

class KSyncLinuxVxlan : public KSyncVxlan {
public:
    KSyncLinuxVxlan(Agent *agent);
    virtual ~KSyncLinuxVxlan() { }

    void Init();
private:
    DISALLOW_COPY_AND_ASSIGN(KSyncLinuxVxlan);
};

#endif // vnsw_agent_linux_vxlan_h
