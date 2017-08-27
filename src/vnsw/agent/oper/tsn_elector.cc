/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <cmn/agent_cmn.h>

#include <base/logging.h>
#include <init/agent_param.h>
#include <oper/operdb_init.h>
#include <oper/route_common.h>
#include <oper/vrf.h>
#include <oper/tsn_elector.h>
#include <oper/bridge_route.h>
#include <oper/inet_unicast_route.h>
#include <oper/agent_route_walker.h>
#include <oper/nexthop.h>
#include <oper/multicast.h>

using namespace std;

TsnElectorState::TsnElectorState(AgentRouteTable *inet4_table,
                                 TsnElector *elector) :
    inet4_table_(inet4_table) {
    inet4_id_ = inet4_table->Register(boost::bind(&TsnElector::RouteNotify,
                                                  elector, _1, _2));
}

TsnElectorState::~TsnElectorState() {
    inet4_table_->Unregister(inet4_id_);
}

TsnElectorWalker::TsnElectorWalker(const std::string &name, Agent *agent) :
    AgentRouteWalker(name, agent) {
}

TsnElectorWalker::~TsnElectorWalker() {
}

bool TsnElectorWalker::RouteWalkNotify(DBTablePartBase *partition,
                                       DBEntryBase *e) {
    BridgeRouteEntry *bridge_entry =
        dynamic_cast<BridgeRouteEntry*>(e);
    if (!bridge_entry || !bridge_entry->is_multicast())
        return true;

    AgentPath *evpn_path = NULL;
    for (Route::PathList::iterator it = bridge_entry->GetPathList().begin();
         it != bridge_entry->GetPathList().end(); it++) {
        AgentPath *path = static_cast<AgentPath *>(it.operator->());
        if (path->peer()->GetType() != Peer::BGP_PEER)
            continue;
        const CompositeNH *cnh = dynamic_cast<const CompositeNH *>(path->nexthop());
        if (!cnh)
            continue;

        if (cnh->composite_nh_type() != Composite::EVPN)
            continue;

        if (!evpn_path)
            evpn_path = path;
        path->set_inactive(!master_);
    }

    if (evpn_path && bridge_entry->ReComputePathAdd(evpn_path))
        partition->Notify(bridge_entry);
    return true;
}

void TsnElectorWalker::LeaveTsnMastership() {
    master_ = false;
    StartVrfWalk();
}

void TsnElectorWalker::AcquireTsnMastership() {
    master_ = true;
    StartVrfWalk();
}

TsnElector::TsnElector(Agent *agent) : agent_(agent),
    vrf_listener_id_(), active_tsn_servers_() {
    if (IsTsnNoForwardingEnabled()) {
        walker_.reset(new TsnElectorWalker("TsnElectorWalker", agent));
    }
}

TsnElector::~TsnElector() {
}

bool TsnElector::IsTsnNoForwardingEnabled() const {
    return (agent_->params()->agent_mode() ==
            AgentParam::TSN_NO_FORWARDING_AGENT);
}

void TsnElector::Register() {
    if (!IsTsnNoForwardingEnabled())
        return;
    vrf_listener_id_ = agent_->vrf_table()->Register(
        boost::bind(&TsnElector::Notify, this, _1, _2));
    agent_->oper_db()->agent_route_walk_manager()->
        RegisterWalker(static_cast<AgentRouteWalker *>(walker_.get()));
}

void TsnElector::Notify(DBTablePartBase *partition, DBEntryBase *e) {
    VrfEntry *vrf = dynamic_cast<VrfEntry *>(e);
    TsnElectorState *state = dynamic_cast<TsnElectorState *>(vrf->
                             GetState(partition->parent(), vrf_listener_id_));
    if (vrf->GetName().compare(agent_->fabric_policy_vrf_name()) != 0)
        return;

    if (vrf->IsDeleted()) {
        if (state) {
            vrf->ClearState(partition->parent(), vrf_listener_id_);
            delete state;
        }
        return;
    }

    if (!state) {
        state = new TsnElectorState(vrf->GetInet4UnicastRouteTable(), this);
        vrf->SetState(partition->parent(), vrf_listener_id_, state);
    }
    return;
}

void TsnElector::RouteNotify(DBTablePartBase *partition, DBEntryBase *e) {
    const InetUnicastRouteEntry *rt =
        static_cast<const InetUnicastRouteEntry*>(e);

    if (!agent_->params()->IsConfiguredTsnHostRoute(rt->addr().to_string()))
        return;

    const string rt_addr_str = rt->GetAddressString();
    std::vector<std::string>::iterator it =
        std::find(active_tsn_servers_.begin(), active_tsn_servers_.end(),
                  rt_addr_str);
    std::string master = "";
    if (!active_tsn_servers_.empty()) {
        master = active_tsn_servers_.front();
    }
    if (rt->IsDeleted()) {
        if (it == active_tsn_servers_.end())
            return;
        active_tsn_servers_.erase(it);
    } else {
        if (it != active_tsn_servers_.end())
            return;
        active_tsn_servers_.push_back(rt_addr_str);
    }
    std::sort(active_tsn_servers_.begin(), active_tsn_servers_.end());
    std::string new_master = "";
    if (!active_tsn_servers_.empty()) {
        new_master = active_tsn_servers_.front();
    }
    if (master == new_master)
        return;

    std::string vhost_addr = agent_->params()->vhost_addr().to_string();
    if (master == vhost_addr) {
        walker()->LeaveTsnMastership();
    }
    if (new_master == vhost_addr) {
        walker()->AcquireTsnMastership();
    }
}

void TsnElector::Shutdown() {
    if (!IsTsnNoForwardingEnabled())
        return;
    agent_->vrf_table()->Unregister(vrf_listener_id_);
    agent_->oper_db()->agent_route_walk_manager()->
        ReleaseWalker(walker_.get());
    walker_.reset(NULL);
}

bool TsnElector::IsMaster() const {
    if (active_tsn_servers_.empty())
        return false;
    return (active_tsn_servers_.front().compare(
        agent_->params()->vhost_addr().to_string()) == 0);
}

const TsnElector::ManagedPhysicalDevicesList &TsnElector::ManagedPhysicalDevices()
const {
    return agent_->oper_db()->multicast()->physical_devices();
}
