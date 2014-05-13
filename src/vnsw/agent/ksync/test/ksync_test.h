/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_ksync_test_h
#define vnsw_agent_ksync_test_h

#include <ksync/ksync_init.h>

class KSyncTest : public KSync {
public:
    KSyncTest(Agent *agent);
    virtual ~KSyncTest();

    virtual void Init(bool create_vhost);
    virtual void RegisterDBClients(DB *db);
    void NetlinkShutdownTest();
private:
    void GenericNetlinkInitTest() const;
    void NetlinkInitTest() const;
    DISALLOW_COPY_AND_ASSIGN(KSyncTest);
};

void GenericNetlinkInitTest();

#endif //vnsw_agent_ksync_test_h
