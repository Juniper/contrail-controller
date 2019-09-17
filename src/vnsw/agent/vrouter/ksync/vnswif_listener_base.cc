/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <assert.h>

#include <base/logging.h>
#include <base/util.h>
#include <cmn/agent_cmn.h>
#include <init/agent_param.h>
#include <init/agent_init.h>
#include <cfg/cfg_init.h>
#include <oper/route_common.h>
#include <oper/mirror_table.h>
#include <ksync/ksync_index.h>
#include <vrouter/ksync/interface_ksync.h>
#include "vrouter/ksync/vnswif_listener_base.h"
#include "net/if.h"
#include <oper/physical_interface.h>
#include <string>

#define INDEX_INTERFACE_NAME 0
#define INDEX_INTERFACE_DRV_NAME 1
#define INDEX_INTERFACE_ID 2
#define NL_MSG_PARAMS 3 
extern void RouterIdDepInit(Agent *agent);

SandeshTraceBufferPtr VnswIfTraceBuf(SandeshTraceBufferCreate(
                                     VNSWIF_TRACE_BUF, 2000));

VnswInterfaceListenerBase::VnswInterfaceListenerBase(Agent *agent) :
    agent_(agent), read_buf_(NULL),
    intf_listener_id_(DBTableBase::kInvalidId),
    fabric_listener_id_(DBTableBase::kInvalidId), seqno_(0),
    vhost_intf_up_(false), ll_addr_table_(), revent_queue_(NULL),
    vhost_update_count_(0), ll_add_count_(0), ll_del_count_(0) {
}

VnswInterfaceListenerBase::~VnswInterfaceListenerBase() {
    if (read_buf_){
        delete [] read_buf_;
        read_buf_ = NULL;
    }
    delete revent_queue_;
}

/****************************************************************************
 * Initialization and shutdown code
 *   1. Opens netlink socket
 *   2. Starts scan of interface from host-os
 *   3. Starts scan of routes from host-os
 *   4. Registers for netlink messages with ASIO
 ****************************************************************************/
void VnswInterfaceListenerBase::Init() {
    intf_listener_id_ = agent_->interface_table()->Register
        (boost::bind(&VnswInterfaceListenerBase::InterfaceNotify, this, _1, _2));

    fabric_listener_id_ = agent_->fabric_inet4_unicast_table()->Register
        (boost::bind(&VnswInterfaceListenerBase::FabricRouteNotify,
                     this, _1, _2));
    vn_listener_id_ = agent_->vn_table()->Register
        (boost::bind(&VnswInterfaceListenerBase::VnNotify,
                     this, _1, _2));


    /* Allocate Route Event Workqueue */
    revent_queue_ = new WorkQueue<Event *>
                    (TaskScheduler::GetInstance()->GetTaskId(kTaskDBExclude), 0,
                     boost::bind(&VnswInterfaceListenerBase::ProcessEvent,
                     this, _1));
    revent_queue_->set_name("Netlink interface listener");
}

void VnswInterfaceListenerBase::Shutdown() {
    agent_->interface_table()->Unregister(intf_listener_id_);
    agent_->fabric_inet4_unicast_table()->Unregister(fabric_listener_id_);
    agent_->vn_table()->Unregister(vn_listener_id_);
    // Expect only one entry for vhost0 during shutdown
    assert(host_interface_table_.size() == 0);
}



void VnswInterfaceListenerBase::Enqueue(Event *event) {
    revent_queue_->Enqueue(event);
}

/****************************************************************************
 * Interface DB notification handler. Triggers add/delete of link-local route
 ****************************************************************************/
void VnswInterfaceListenerBase::InterfaceNotify(DBTablePartBase *part,
                                            DBEntryBase *e) {
    const VmInterface *vmport = dynamic_cast<VmInterface *>(e);
    if (vmport == NULL) {
        return;
    }

    DBState *state = e->GetState(part->parent(), intf_listener_id_);

    if (vmport->IsDeleted()) {
        if (state) {
            HostInterfaceEntry *entry = GetHostInterfaceEntry(vmport->name());
            uint32_t id = Interface::kInvalidIndex;
            if (entry) {
                id = entry->oper_id_;
            }
            std::ostringstream oss;
            oss << "Intf Del " << vmport->name() << " id " << id;
            string msg = oss.str();
            VNSWIF_TRACE(msg.c_str());
            ResetSeen(vmport->name(), true);
            e->ClearState(part->parent(), intf_listener_id_);
            delete state;
        }
    } else {
        if (state == NULL) {
            std::ostringstream oss;
            oss << "Intf Add " << vmport->name() << " id " << vmport->id();
            string msg = oss.str();
            VNSWIF_TRACE(msg.c_str());
            state = new DBState();
            e->SetState(part->parent(), intf_listener_id_, state);
            SetSeen(vmport->name(), true, vmport->id());
        }
    }

    return;
}

/****************************************************************************
 * Fabric unicast route notification handler. Triggers add/delete of
 * link-local route
 ****************************************************************************/
void VnswInterfaceListenerBase::FabricRouteNotify(DBTablePartBase *part,
                                                  DBEntryBase *e) {
    const InetUnicastRouteEntry *rt = dynamic_cast<InetUnicastRouteEntry *>(e);
    if (rt == NULL || rt->GetTableType() != Agent::INET4_UNICAST) {
        return;
    }

    DBState *state = e->GetState(part->parent(), fabric_listener_id_);

    if (rt->IsDeleted()) {
        if (state) {
            e->ClearState(part->parent(), fabric_listener_id_);
            delete state;
            revent_queue_->Enqueue(new Event(Event::DEL_LL_ROUTE,
                                             "", rt->addr().to_v4()));
        }
    } else {
        // listen to only metadata ip routes
        const AgentPath *path = rt->GetActivePath();
        if (path == NULL || path->peer() != agent_->link_local_peer()) {
            return;
        }

        if (state == NULL) {
            state = new DBState();
            e->SetState(part->parent(), fabric_listener_id_, state);
            revent_queue_->Enqueue(new Event(Event::ADD_LL_ROUTE,
                                             "", rt->addr().to_v4()));
        }
    }

    return;
}

void VnswInterfaceListenerBase::VnDBState::Enqueue(VnswInterfaceListenerBase *base,
                                                   const VnIpam &entry,
                                                   const Event::Type event) {
    std::stringstream str;
    str << entry.ip_prefix << entry.plen;

    //If entry matches vhost subnet do nothing.
    InetUnicastAgentRouteTable *table =
        base->agent()->fabric_vrf()->GetInet4UnicastRouteTable();
    InetUnicastRouteEntry *rt =
        table->FindResolveRoute(entry.ip_prefix.to_v4());
    if (rt && rt->plen() >= entry.plen) {
        return;
    }

    bool enqueue = true;
    if (event == Event::ADD_LL_ROUTE) {
        enqueue = base->AddIpam(entry.ip_prefix.to_v4(), entry.plen);
    } else {
        enqueue = base->DelIpam(entry.ip_prefix.to_v4(), entry.plen);
    }

    if (enqueue) {
        base->revent_queue_->Enqueue(new Event(event,
                                     "", entry.ip_prefix.to_v4(),
                                     entry.plen, 0, true));
    }
}

void VnswInterfaceListenerBase::VnDBState::Add(VnswInterfaceListenerBase *base,
                                               const VnEntry *vn) {
    std::set<VnIpam> old_ipam_list = ipam_list_;

    std::vector<VnIpam>::const_iterator it = vn->GetVnIpam().begin();
    for(; it != vn->GetVnIpam().end(); it++) {
        if (it->ip_prefix.is_v4() == false) {
            continue;
        }

        if (ipam_list_.find(*it) != ipam_list_.end()) {
            old_ipam_list.erase(*it);
        } else {
            ipam_list_.insert(*it);
            Enqueue(base, *it, Event::ADD_LL_ROUTE);
        }
    }

    std::set<VnIpam>::iterator oit = old_ipam_list.begin();
    for(; oit != old_ipam_list.end(); oit++) {
        Enqueue(base, *oit, Event::DEL_LL_ROUTE);
    }
}

void VnswInterfaceListenerBase::VnDBState::Delete(VnswInterfaceListenerBase *base) {
    std::set<VnIpam>::iterator it = ipam_list_.begin();
    for(; it != ipam_list_.end(); it++) {
        Enqueue(base, *it, Event::DEL_LL_ROUTE);
    }
}

void VnswInterfaceListenerBase::VnNotify(DBTablePartBase *part,
                                         DBEntryBase *e) {
    VnDBState *state =
        static_cast<VnDBState *>(e->GetState(part->parent(), vn_listener_id_));
    VnEntry *vn = static_cast<VnEntry *>(e);

    if (e->IsDeleted() || vn->underlay_forwarding() == false) {
        if (state) {
            e->ClearState(part->parent(), vn_listener_id_);
            state->Delete(this);
            delete state;
        }
    } else if (vn->underlay_forwarding()) {
        if (state == NULL) {
            state = new VnDBState();
        }
        e->SetState(part->parent(), vn_listener_id_, state);
        state->Add(this, vn);
    }
    return;
}
/****************************************************************************
 * Interface Event handler
 ****************************************************************************/
bool VnswInterfaceListenerBase::IsInterfaceActive(const HostInterfaceEntry *entry) {
    return entry->oper_seen_ && entry->host_seen_ && entry->link_up_;
}

static void InterfaceResync(Agent *agent, uint32_t id, bool active,
                            bool link_status) {
    if (id == Interface::kInvalidIndex) {
        return;
    }
    InterfaceTable *table = agent->interface_table();
    Interface *intrface = table->FindInterface(id);
    if (intrface == NULL) {
        std::ostringstream oss;
        oss << "InterfaceResync failed. Interface index " << id <<
            " not found. Active " << active;
        string msg = oss.str();
        VNSWIF_TRACE(msg.c_str());
        return;
    }

    /* In VmWare mode no need to notifiy on activate/deactivate. This is handled
     * via rest requrests enable-port/disable-port
     */
    if (intrface->NeedDefaultOsOperStateDisabled(agent)) {
        return;
    }
    if (agent->test_mode())
        intrface->set_test_oper_state(active);
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new VmInterfaceKey(AgentKey::RESYNC, intrface->GetUuid(),
                                     intrface->name()));
    string link_state_str = link_status? "up" : "down";
    std::ostringstream oss;
    oss << "InterfaceResync for id " << id << " link_state " << link_state_str;
    VNSWIF_TRACE(oss.str().c_str());
    req.data.reset(new VmInterfaceOsOperStateData(link_status));
    table->Enqueue(&req);
}

VnswInterfaceListenerBase::HostInterfaceEntry *
VnswInterfaceListenerBase::GetHostInterfaceEntry(const std::string &name) {
    HostInterfaceTable::iterator it = host_interface_table_.find(name);
    if (it == host_interface_table_.end())
        return NULL;
    return it->second;
}

// Handle transition from In-Active to Active interface
void VnswInterfaceListenerBase::Activate(const std::string &name,
                                         const HostInterfaceEntry *entry) {
    if (name == agent_->vhost_interface_name()) {
        // link-local routes would have been deleted when vhost link was down.
        // Add all routes again on activation
        AddLinkLocalRoutes();
        //Add IPAM routes also
        AddIpamRoutes();
    }
    if (entry == NULL) {
        return;
    }
    InterfaceResync(agent_, entry->oper_id_, true, entry->link_up_);
}

void VnswInterfaceListenerBase::DeActivate(const std::string &name,
                                           const HostInterfaceEntry *entry) {
    if (entry == NULL) {
        return;
    }
    InterfaceResync(agent_, entry->oper_id_, false, entry->link_up_);
}

void VnswInterfaceListenerBase::SetSeen(const std::string &name, bool oper,
                                        uint32_t id) {
    HostInterfaceEntry *entry = GetHostInterfaceEntry(name);
    if (entry == NULL) {
        entry = new HostInterfaceEntry();
        host_interface_table_.insert(make_pair(name, entry));
    }

    if (oper) {
        entry->oper_seen_ = true;
        entry->oper_id_ = id;
    } else {
        entry->host_seen_ = true;
    }

    Activate(name, entry);
}

void VnswInterfaceListenerBase::ResetSeen(const std::string &name, bool oper) {
    HostInterfaceEntry *entry = GetHostInterfaceEntry(name);
    if (entry == NULL)
        return;

    if (oper) {
        entry->oper_seen_ = false;
        entry->oper_id_ = Interface::kInvalidIndex;
    } else {
        entry->host_seen_ = false;
    }

    if (entry->oper_seen_ == false && entry->host_seen_ == false) {
        HostInterfaceTable::iterator it = host_interface_table_.find(name);
        host_interface_table_.erase(it);
        delete entry;
        return;
    }

    if (!oper) {
        /* Send notification to interface only when it is not marked for
         * delete */
        DeActivate(name, entry);
    }
}

void VnswInterfaceListenerBase::SetLinkState(const std::string &name, bool link_up){
    HostInterfaceEntry *entry = GetHostInterfaceEntry(name);
    if (entry == NULL)
        return;

    bool old_active = IsInterfaceActive(entry);
    entry->link_up_ = link_up;

    if (old_active)
        DeActivate(name, entry);
    else
        Activate(name, entry);
}

void VnswInterfaceListenerBase::HandleInterfaceEvent(const Event *event) {
    if (event->event_ == Event::DEL_INTERFACE) {
        ResetSeen(event->interface_, false);
    } else {
        bool up = (event->flags_ & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING);
        if((event->type_ == VnswInterfaceListenerBase::VR_FABRIC) ||
            (event->type_ == VnswInterfaceListenerBase::VR_BOND_SLAVES))
        {
            std::vector<std::string> interface_info;
            std::istringstream iss(event->interface_);
            for(std::string s; iss >> s; )
                interface_info.push_back(s);
            /* Vrouter is sending a netlink message. In that message
             * event_>interface_ will contain intf name, intf drv name, and
             * intf_id. So breaking this message and store these values in a
             * vector. So in vector 1st value will be intf_name, 2nd is
             * intf_drv_name and 3rd is intf_id. Total 3 parameters vrouter will
             * send, it will come in event_>interface_.
             */
            InterfaceTable *table = agent_->interface_table();
            if(table)
            {
                if(interface_info.size() == NL_MSG_PARAMS)
                {
                  /* Used NL_MSG_PARAMS macro to determine
                   * the no of parameters vrouter will send i.e 3
                   */
                    const char *x = interface_info[INDEX_INTERFACE_ID].c_str();
                    Interface *interface = table->FindInterface(atoi(x));
                    if(interface)
                    {
                        PhysicalInterface *phy_intf =
                            dynamic_cast<PhysicalInterface *>(interface);
                        if(phy_intf)
                        {
                            DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
                            req.key.reset(new
                                    PhysicalInterfaceKey(
                                        agent_->fabric_interface_name()));
                            req.data.reset(new
                                    PhysicalInterfaceOsOperStateData(
                                        event->type_,
                                        interface_info[INDEX_INTERFACE_NAME],
                                        interface_info[INDEX_INTERFACE_DRV_NAME],
                                        up));
                            table->Enqueue(&req);
                        }
                    }
                }
            }
        }
        SetSeen(event->interface_, false, Interface::kInvalidIndex);

        SetLinkState(event->interface_, up);

        // In XEN mode, notify add of XAPI interface
        if (agent_->isXenMode()) {
            if (string::npos != event->interface_.find(XAPI_INTF_PREFIX)) {
                agent_->params()->set_xen_ll_name(event->interface_);
                agent_->InitXenLinkLocalIntf();
            }
        }
    }
}

bool VnswInterfaceListenerBase::IsValidLinkLocalAddress(const Ip4Address &addr)
    const {
    return (ll_addr_table_.find(addr) != ll_addr_table_.end());
}

/****************************************************************************
 * Interface Address event handler
 ****************************************************************************/
void VnswInterfaceListenerBase::SetAddress(const Event *event) {
    HostInterfaceEntry *entry = GetHostInterfaceEntry(event->interface_);
    if (entry == NULL) {
        entry = new HostInterfaceEntry();
        host_interface_table_.insert(make_pair(event->interface_, entry));
    }

    entry->addr_ = event->addr_;
    entry->plen_ = event->plen_;
}

void VnswInterfaceListenerBase::ResetAddress(const Event *event) {
    HostInterfaceEntry *entry = GetHostInterfaceEntry(event->interface_);
    if (entry == NULL) {
        return;
    }

    entry->addr_ = Ip4Address(0);
    entry->plen_ = 0;
}

void VnswInterfaceListenerBase::HandleAddressEvent(const Event *event) {
    // Update address in interface table
    if (event->event_ == Event::ADD_ADDR) {
        SetAddress(event);
    } else {
        ResetAddress(event);
    }

    // We only handle IP Address add for VHOST interface
    // We dont yet handle delete of IP address or change of IP address
    if (event->event_ != Event::ADD_ADDR ||
        event->interface_ != agent_->vhost_interface_name() ||
        event->addr_.to_ulong() == 0) {
        return;
    }

    // Check if vhost already has address. We cant handle IP address change yet
    const VmInterface *vhost =
        static_cast<const VmInterface *>(agent_->vhost_interface());
    if (vhost->primary_ip_addr() != Ip4Address(0)) {
        return;
    }

    std::ostringstream oss;
    oss << "Setting IP address for " << event->interface_ << " to "
        << event->addr_.to_string() << "/" << (unsigned short)event->plen_;
    string msg = oss.str();
    VNSWIF_TRACE(msg.c_str());
    vhost_update_count_++;

    bool dep_init_reqd = false;
    if (agent_->router_id_configured() == false)
        dep_init_reqd = true;

    // Update vhost ip-address and enqueue db request
    agent_->set_router_id(event->addr_);
    agent_->set_vhost_prefix_len(event->plen_);

    agent_->interface_table()->CreateVhostReq();

    if (dep_init_reqd)
        agent_->agent_init()->ConnectToControllerBase();
}

void
VnswInterfaceListenerBase::UpdateLinkLocalRouteAndCount(
    const Ip4Address &addr, uint8_t plen, bool del_rt)
{
    if (del_rt)
        ll_del_count_++;
    else
        ll_add_count_++;
}

// Handle link-local route changes resulting from ADD_LL_ROUTE or DEL_LL_ROUTE
void VnswInterfaceListenerBase::LinkLocalRouteFromLinkLocalEvent(Event *event) {
    if (event->event_ == Event::DEL_LL_ROUTE) {
        if (event->ipam_ == false) {
            ll_addr_table_.erase(event->addr_);
        }
        UpdateLinkLocalRouteAndCount(event->addr_, event->plen_, true);
    } else {
        if (event->ipam_ == false) {
            ll_addr_table_.insert(event->addr_);
        }
        UpdateLinkLocalRouteAndCount(event->addr_, event->plen_, false);
    }
}

// For link-local routes added by agent, we treat agent as master. So, force
// add the routes again if they are deleted from kernel
void VnswInterfaceListenerBase::LinkLocalRouteFromRouteEvent(Event *event) {
    if (event->protocol_ != kVnswRtmProto)
        return;

    if (ll_addr_table_.find(event->addr_) == ll_addr_table_.end()) {
        // link-local route is in kernel, but not in agent. This can happen
        // when agent has restarted and the link-local route is not valid
        // after agent restart.
        // Delete the route
        if (event->event_ == Event::ADD_ROUTE) {
            UpdateLinkLocalRouteAndCount(event->addr_, Address::kMaxV4PrefixLen,
                                         true);
        }
        return;
    }

    if ((event->event_ == Event::DEL_ROUTE) ||
        (event->event_ == Event::ADD_ROUTE
         && event->interface_ != agent_->vhost_interface_name())) {
        UpdateLinkLocalRouteAndCount(event->addr_, Address::kMaxV4PrefixLen,
                                     false);
    }
}

void VnswInterfaceListenerBase::AddLinkLocalRoutes() {
    for (LinkLocalAddressTable::iterator it = ll_addr_table_.begin();
         it != ll_addr_table_.end(); ++it) {
        UpdateLinkLocalRouteAndCount(*it, Address::kMaxV4PrefixLen, false);
    }
}

void VnswInterfaceListenerBase::DelLinkLocalRoutes() {
}

void VnswInterfaceListenerBase::AddIpamRoutes() {
    IpamSubnetMap::const_iterator it = ipam_subnet_.begin();
    for(; it != ipam_subnet_.end(); it++) {
        UpdateLinkLocalRouteAndCount(it->first.ip_, it->first.plen_, false);
    }
}

/****************************************************************************
 * Event handler
 ****************************************************************************/
static string EventTypeToString(uint32_t type) {
    const char *name[] = {
        "INVALID",
        "ADD_ADDR",
        "DEL_ADDR",
        "ADD_INTERFACE",
        "DEL_INTERFACE",
        "ADD_ROUTE",
        "DEL_ROUTE",
        "ADD_LL_ROUTE",
        "DEL_LL_ROUTE"
    };

    if (type < sizeof(name)/sizeof(void *)) {
        return name[type];
    }

    return "UNKNOWN";
}

bool VnswInterfaceListenerBase::ProcessEvent(Event *event) {
    HostInterfaceEntry *entry = GetHostInterfaceEntry(event->interface_);
    uint32_t id = Interface::kInvalidIndex;
    if (entry) {
        id = entry->oper_id_;
    }
    std::ostringstream oss;
    oss << " Event " << EventTypeToString(event->event_)
        << " Interface " << event->interface_ << " Addr "
        << event->addr_.to_string() << " prefixlen " << (uint32_t)event->plen_
        << " Gateway " << event->gw_.to_string() << " Flags " << event->flags_
        << " Protocol " << (uint32_t)event->protocol_ << " Index " << id;
    string msg = oss.str();
    VNSWIF_TRACE(msg.c_str());

    switch (event->event_) {
    case Event::ADD_ADDR:
    case Event::DEL_ADDR:
        HandleAddressEvent(event);
        break;

    case Event::ADD_INTERFACE:
    case Event::DEL_INTERFACE:
        HandleInterfaceEvent(event);
        break;

    case Event::ADD_ROUTE:
    case Event::DEL_ROUTE:
        LinkLocalRouteFromRouteEvent(event);
        break;

    case Event::ADD_LL_ROUTE:
    case Event::DEL_LL_ROUTE:
        LinkLocalRouteFromLinkLocalEvent(event);
        break;

    default:
        break;
    }

    delete event;
    return true;
}
