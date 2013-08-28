/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_ksync_init_h
#define vnsw_agent_ksync_init_h

class KSync {
public:
    static void RegisterDBClients(DB *db);
    static void NetlinkInit();
    static void VRouterInterfaceSnapshot();
    static void ResetVRouter();
    static void VnswIfListenerInit();
    static void CreateVhostIntf();
    static void Shutdown();

    static void RegisterDBClientsTest(DB *db);
    static void NetlinkInitTest();
    static void NetlinkShutdownTest();
    static void UpdateVhostMac();
};

int GenenericNetlinkFamily();
void GenericNetlinkInit();
void GenericNetlinkInitTest();

#endif //vnsw_agent_ksync_init_h
