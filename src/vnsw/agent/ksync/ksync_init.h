/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_ksync_init_h
#define vnsw_agent_ksync_init_h

#include <ksync/flowtable_ksync.h>
#include <ksync/mpls_ksync.h>
#include <ksync/nexthop_ksync.h>
#include <ksync/mirror_ksync.h>
#include <ksync/route_ksync.h>
#include <ksync/vxlan_ksync.h>
#include <ksync/vrf_assign_ksync.h>
#include <ksync/interface_scan.h>
#include "vnswif_listener.h"

class KSync {
public:
    KSync(Agent *agent);
    virtual ~KSync();

    virtual void Init(bool create_vhost);
    virtual void RegisterDBClients(DB *db);
    void VnswInterfaceListenerInit();
    void Shutdown();

    void UpdateVhostMac();
    Agent *agent() const  { return agent_; }
    MirrorKSyncObject *mirror_ksync_obj() const { 
        return mirror_ksync_obj_.get(); 
    }
    NHKSyncObject *nh_ksync_obj() const {
        return nh_ksync_obj_.get();
    }
    InterfaceKSyncObject *interface_ksync_obj() const {
        return interface_ksync_obj_.get();
    }
    VrfKSyncObject *vrf_ksync_obj() const {
        return vrf_ksync_obj_.get();
    }
    FlowTableKSyncObject *flowtable_ksync_obj() const {
        return flowtable_ksync_obj_.get();
    }
    InterfaceKScan *interface_scanner() const {
        return interface_scanner_.get();
    }
    VnswInterfaceListener *vnsw_interface_listner() const  {
        return vnsw_interface_listner_.get();
    }
protected:
    Agent *agent_;
    boost::scoped_ptr<InterfaceKSyncObject> interface_ksync_obj_; 
    boost::scoped_ptr<FlowTableKSyncObject> flowtable_ksync_obj_; 
    boost::scoped_ptr<MplsKSyncObject> mpls_ksync_obj_; 
    boost::scoped_ptr<NHKSyncObject> nh_ksync_obj_; 
    boost::scoped_ptr<MirrorKSyncObject> mirror_ksync_obj_; 
    boost::scoped_ptr<VrfKSyncObject> vrf_ksync_obj_;
    boost::scoped_ptr<VxLanKSyncObject> vxlan_ksync_obj_;
    boost::scoped_ptr<VrfAssignKSyncObject> vrf_assign_ksync_obj_;
    boost::scoped_ptr<InterfaceKScan> interface_scanner_;
    boost::scoped_ptr<VnswInterfaceListener> vnsw_interface_listner_;
private:
    void InitFlowMem();
    void NetlinkInit();
    void VRouterInterfaceSnapshot();
    void ResetVRouter();
    void CreateVhostIntf();
    int Encode(Sandesh &encoder, uint8_t *buf, int buf_len);
    DISALLOW_COPY_AND_ASSIGN(KSync);
};

int GenenericNetlinkFamily();
void GenericNetlinkInit();

#endif //vnsw_agent_ksync_init_h
