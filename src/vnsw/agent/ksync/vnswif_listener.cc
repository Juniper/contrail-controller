/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <assert.h>
//#include <sys/socket.h>
//#include <netinet/in.h>
#include "vr_os.h"

//#include <ifaddrs.h>
//#include <strings.h>
//#include <netinet/in.h>
//#include <arpa/inet.h>
//#include <errno.h>
//#include <net/if.h>
//#include <sys/ioctl.h>
//#include <net/address.h>

#include <base/logging.h>
#include <base/util.h>
#include <cmn/agent_cmn.h>
#include <cmn/agent_param.h>
#include <cfg/cfg_init.h>
#include <oper/route_common.h>
#include <oper/mirror_table.h>
#include <ksync/ksync_index.h>
#include <ksync/interface_ksync.h>
#include <ksync/vnswif_listener.h>

extern void RouterIdDepInit(Agent *agent);

VnswInterfaceListenerBase::VnswInterfaceListenerBase(Agent *agent) : 
    agent_(agent), read_buf_(NULL), sock_fd_(-1), 
    sock_(*(agent->event_manager())->io_service()),
    intf_listener_id_(DBTableBase::kInvalidId), seqno_(0),
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

    /* Allocate Route Event Workqueue */
    revent_queue_ = new WorkQueue<Event *>
                    (TaskScheduler::GetInstance()->GetTaskId("db::DBTable"), 0,
                     boost::bind(&VnswInterfaceListenerBase::ProcessEvent, 
                     this, _1));

    if (agent_->test_mode())
        return;

    /* Create socket and listen and handle ip address updates */
    sock_fd_ = CreateSocket();

    /* Assign native socket to boost asio */
    boost::asio::local::datagram_protocol protocol;
    sock_.assign(protocol, sock_fd_);

    SyncCurrentState();

    RegisterAsyncReadHandler();
}

void VnswInterfaceListenerBase::Shutdown() { 
    // Expect only one entry for vhost0 during shutdown
    assert(host_interface_table_.size() <= 1);
    for (HostInterfaceTable::iterator it = host_interface_table_.begin();
         it != host_interface_table_.end(); it++) {
        it->second = NULL;
    }
    host_interface_table_.clear();
    if (agent_->test_mode()) {
        return;
    }

    boost::system::error_code ec;
    sock_.close(ec);
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

    DBState *s = e->GetState(part->parent(), intf_listener_id_);
    State *state = static_cast<State *>(s);

    // Get old and new addresses
    Ip4Address addr = vmport->mdata_ip_addr();
    if (vmport->IsDeleted() || vmport->ipv4_active() == false) {
        addr = Ip4Address(0);
    }

    Ip4Address old_addr(0);
    if (state) {
        old_addr = Ip4Address(state->addr_);
    }

    if (vmport->IsDeleted()) {
        if (state) {
            ResetSeen(vmport->name(), true);
            e->ClearState(part->parent(), intf_listener_id_);
            delete state;
        }
    } else {
        if (state == NULL) {
            state = new State(vmport->mdata_ip_addr());
            e->SetState(part->parent(), intf_listener_id_, state);
            SetSeen(vmport->name(), true);
            HostInterfaceEntry *entry = GetHostInterfaceEntry(vmport->name());
            entry->oper_id_ = vmport->id();
        } else {
            state->addr_ = addr;
        }
    }

    if (addr != old_addr) {
        if (old_addr.to_ulong()) {
            revent_queue_->Enqueue(new Event(Event::DEL_LL_ROUTE,
                                             vmport->name(), old_addr));
        }

        if (addr.to_ulong()) {
            revent_queue_->Enqueue(new Event(Event::ADD_LL_ROUTE,
                                             vmport->name(), addr));
        }
    }
    return;
}

/****************************************************************************
 * Interface Event handler
 ****************************************************************************/
bool VnswInterfaceListenerBase::IsInterfaceActive(const HostInterfaceEntry *entry) {
    return entry->oper_seen_ && entry->host_seen_ && entry->link_up_;
}

static void InterfaceResync(Agent *agent, uint32_t id, bool active) {
    InterfaceTable *table = agent->interface_table();
    Interface *interface = table->FindInterface(id);
    if (interface == NULL)
        return;

    if (agent->test_mode())
        interface->set_test_oper_state(active);
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new VmInterfaceKey(AgentKey::RESYNC, interface->GetUuid(),
                                     interface->name()));
    req.data.reset(new VmInterfaceOsOperStateData());
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
void VnswInterfaceListenerBase::Activate(const std::string &name, uint32_t id) {
    if (name == agent_->vhost_interface_name()) {
        // link-local routes would have been deleted when vhost link was down.
        // Add all routes again on activation
        AddLinkLocalRoutes();
    }
    InterfaceResync(agent_, id, true);
}

void VnswInterfaceListenerBase::DeActivate(const std::string &name, uint32_t id){
    InterfaceResync(agent_, id, false);
}

void VnswInterfaceListenerBase::SetSeen(const std::string &name, bool oper) {
    HostInterfaceEntry *entry = GetHostInterfaceEntry(name);
    if (entry == NULL) {
        entry = new HostInterfaceEntry();
        host_interface_table_.insert(make_pair(name, entry));
    }

    bool old_active = IsInterfaceActive(entry);
    if (oper) {
        entry->oper_seen_ = true;
    } else {
        entry->host_seen_ = true;
    }

    if (old_active == IsInterfaceActive(entry))
        return;

    if (old_active == false)
        Activate(name, entry->oper_id_);
}

void VnswInterfaceListenerBase::ResetSeen(const std::string &name, bool oper) {
    HostInterfaceEntry *entry = GetHostInterfaceEntry(name);
    if (entry == NULL)
        return;

    bool old_active = IsInterfaceActive(entry);
    if (oper) {
        entry->oper_seen_ = false;
    } else {
        entry->host_seen_ = false;
    }

    if (entry->oper_seen_ == false && entry->host_seen_ == false) {
        HostInterfaceTable::iterator it = host_interface_table_.find(name);
        host_interface_table_.erase(it);
        delete entry;
        return;
    }

    if (old_active == IsInterfaceActive(entry))
        return;

    if (old_active)
        DeActivate(name, entry->oper_id_);
}

void VnswInterfaceListenerBase::SetLinkState(const std::string &name, bool link_up){
    HostInterfaceEntry *entry = GetHostInterfaceEntry(name);
    if (entry == NULL)
        return;

    bool old_active = IsInterfaceActive(entry);
    entry->link_up_ = link_up;

    if (old_active == IsInterfaceActive(entry))
        return;

    if (old_active)
        DeActivate(name, entry->oper_id_);
    else
        Activate(name, entry->oper_id_);
}

void VnswInterfaceListenerBase::HandleInterfaceEvent(const Event *event) {
    if (event->event_ == Event::DEL_INTERFACE) {
        ResetSeen(event->interface_, false);
    } else {
        SetSeen(event->interface_, false);
        bool up = 
            (event->flags_ & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING);
                                    

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
    const InetInterface *vhost = 
        static_cast<const InetInterface *>(agent_->vhost_interface());
    if (vhost->ip_addr().to_ulong() != 0) {
        return;
    }

    LOG(DEBUG, "Setting IP address for " << event->interface_ << " to " 
        << event->addr_.to_string() << "/" << (unsigned short)event->plen_);
    vhost_update_count_++;

    bool dep_init_reqd = false;
    if (agent_->router_id_configured() == false)
        dep_init_reqd = true;

    // Update vhost ip-address and enqueue db request
    agent_->set_router_id(event->addr_);
    agent_->set_vhost_prefix_len(event->plen_);
    InetInterface::CreateReq(agent_->interface_table(),
                             agent_->vhost_interface_name(),
                             InetInterface::VHOST, agent_->fabric_vrf_name(),
                             event->addr_, event->plen_,
                             agent_->vhost_default_gateway(),
                             Agent::NullString(), agent_->fabric_vrf_name());
    if (dep_init_reqd)
        RouterIdDepInit(agent_);
}

void
VnswInterfaceListenerBase::UpdateLinkLocalRouteAndCount(
    const Ip4Address &addr, bool del_rt) 
{
    if (del_rt)
        ll_del_count_++;
    else
        ll_add_count_++;
    if (agent_->test_mode())
        return;

    UpdateLinkLocalRoute(addr, del_rt);
}

// Handle link-local route changes resulting from ADD_LL_ROUTE or DEL_LL_ROUTE
void VnswInterfaceListenerBase::LinkLocalRouteFromLinkLocalEvent(Event *event) {
    if (event->event_ == Event::DEL_LL_ROUTE) {
        ll_addr_table_.erase(event->addr_);
        UpdateLinkLocalRouteAndCount(event->addr_, true); } else {
        ll_addr_table_.insert(event->addr_);
        UpdateLinkLocalRouteAndCount(event->addr_, false);
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
            UpdateLinkLocalRouteAndCount(event->addr_, true);
        }
        return;
    }

    if ((event->event_ == Event::DEL_ROUTE) ||
        (event->event_ == Event::ADD_ROUTE 
         && event->interface_ != agent_->vhost_interface_name())) {
        UpdateLinkLocalRouteAndCount(event->addr_, false);
    }
}

void VnswInterfaceListenerBase::AddLinkLocalRoutes() {
    for (LinkLocalAddressTable::iterator it = ll_addr_table_.begin();
         it != ll_addr_table_.end(); ++it) {
        UpdateLinkLocalRouteAndCount(*it, false);
    }
}

void VnswInterfaceListenerBase::DelLinkLocalRoutes() {
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
    LOG(DEBUG, "VnswInterfaceListenerBase Event " << EventTypeToString(event->event_) 
        << " Interface " << event->interface_ << " Addr "
        << event->addr_.to_string() << " prefixlen " << (uint32_t)event->plen_
        << " Gateway " << event->gw_.to_string() << " Flags " << event->flags_
        << " Protocol " << (uint32_t)event->protocol_);

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
