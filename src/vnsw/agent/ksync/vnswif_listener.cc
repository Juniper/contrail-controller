/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <net/address.h>

#include <base/logging.h>
#include <base/util.h>
#include <cmn/agent_cmn.h>
#include <init/agent_param.h>
#include <init/agent_init.h>
#include <cfg/cfg_init.h>
#include <oper/route_common.h>
#include <oper/mirror_table.h>
#include <ksync/ksync_index.h>
#include <ksync/interface_ksync.h>
#include <ksync/vnswif_listener.h>

extern void RouterIdDepInit(Agent *agent);

VnswInterfaceListener::VnswInterfaceListener(Agent *agent) : 
    agent_(agent), read_buf_(NULL), sock_fd_(0), 
    sock_(*(agent->GetEventManager())->io_service()),
    intf_listener_id_(DBTableBase::kInvalidId), seqno_(0),
    vhost_intf_up_(false), ll_addr_table_(), revent_queue_(NULL),
    netlink_msg_tx_count_(0), vhost_update_count_(0) { 
}

VnswInterfaceListener::~VnswInterfaceListener() {
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
void VnswInterfaceListener::Init() {
    intf_listener_id_ = agent_->GetInterfaceTable()->Register
        (boost::bind(&VnswInterfaceListener::InterfaceNotify, this, _1, _2));

    /* Allocate Route Event Workqueue */
    revent_queue_ = new WorkQueue<Event *>
                    (TaskScheduler::GetInstance()->GetTaskId("db::DBTable"), 0,
                     boost::bind(&VnswInterfaceListener::ProcessEvent, 
                                 this, _1));

    if (agent_->test_mode())
        return;

    /* Create socket and listen and handle ip address updates */
    CreateSocket();

    /* Fetch Links from kernel syncronously, to allow dump request for routes
     * to go through fine
     */
    InitNetlinkScan(RTM_GETLINK, ++seqno_);

    /* Fetch routes from kernel asyncronously and update the gateway-id */
    InitNetlinkScan(RTM_GETADDR, ++seqno_);

    /* Fetch routes from kernel asyncronously and update the gateway-id */
    InitNetlinkScan(RTM_GETROUTE, ++seqno_);

    RegisterAsyncHandler();
}

void VnswInterfaceListener::Shutdown() { 

    if (agent_->test_mode())
        return;

    boost::system::error_code ec;
    sock_.close(ec);
}

// Create netlink socket
void VnswInterfaceListener::CreateSocket() {
    sock_fd_ = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
    if (sock_fd_ < 0) {
        LOG(ERROR, "Error <" << errno << ": " << strerror(errno) << 
                "> creating socket");
        assert(0);
    }

    /* Bind to netlink socket */
    struct sockaddr_nl addr;
    memset (&addr,0,sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = (RTMGRP_IPV4_ROUTE | RTMGRP_LINK | RTMGRP_IPV4_IFADDR);
    if (bind(sock_fd_, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        LOG(ERROR, "Error <" << errno << ": " << strerror(errno) << 
                       "> binding to netlink address family");
        assert(0);
    }

    /* Assign native socket to boost asio */
    boost::asio::local::datagram_protocol protocol;
    sock_.assign(protocol, sock_fd_);
}

// Initiate netlink scan based on type and flags
void VnswInterfaceListener::InitNetlinkScan(uint32_t type, uint32_t seqno) {
    struct nlmsghdr *nlh;
    const uint32_t buf_size = VnswInterfaceListener::kMaxBufferSize;

    memset(tx_buf_, 0, buf_size);
    nlh = (struct nlmsghdr *)tx_buf_;

    /* Fill in the nlmsg header */
    nlh->nlmsg_len = NLMSG_LENGTH(sizeof(struct rtgenmsg));
    nlh->nlmsg_type = type;
    // The message is a request for dump.
    nlh->nlmsg_flags = (NLM_F_DUMP | NLM_F_REQUEST);
    nlh->nlmsg_seq = seqno;

    struct rtgenmsg *rt_gen = (struct rtgenmsg *) NLMSG_DATA (nlh);
    rt_gen->rtgen_family = AF_PACKET;

    boost::system::error_code ec;
    sock_.send(boost::asio::buffer(nlh,nlh->nlmsg_len), 0, ec);
    assert(ec.value() == 0);

    uint8_t read_buf[buf_size];

    /* 
     * Wait/Read the response for dump request, linux kernel doesn't handle 
     * dump request if response for previous dump request is not complete. 
     */
    int end = 0;
    while (end == 0) {
        memset(read_buf, 0, buf_size);
        std::size_t len = sock_.receive(boost::asio::buffer(read_buf,
                                                            buf_size), 0, ec);
        assert(ec.value() == 0);
        struct nlmsghdr *nl = (struct nlmsghdr *)read_buf;
        end = NlMsgDecode(nl, len, seqno);
    }
}

void VnswInterfaceListener::RegisterAsyncHandler() {
    read_buf_ = new uint8_t[kMaxBufferSize];
    sock_.async_receive(boost::asio::buffer(read_buf_, kMaxBufferSize), 
                        boost::bind(&VnswInterfaceListener::ReadHandler, this,
                                 boost::asio::placeholders::error,
                                 boost::asio::placeholders::bytes_transferred));
}

void VnswInterfaceListener::ReadHandler(const boost::system::error_code &error,
                                 std::size_t len) {
    struct nlmsghdr *nlh;

    if (error == 0) {
        nlh = (struct nlmsghdr *)read_buf_;
        NlMsgDecode(nlh, len, -1);
    } else {
        LOG(ERROR, "Error < : " << error.message() << 
                   "> reading packet on netlink sock");
    }

    if (read_buf_) {
        delete [] read_buf_;
        read_buf_ = NULL;
    }
    RegisterAsyncHandler();
}

void VnswInterfaceListener::Enqueue(Event *event) {
    revent_queue_->Enqueue(event);
}

/****************************************************************************
 * Interface DB notification handler. Triggers add/delete of link-local route
 ****************************************************************************/
void VnswInterfaceListener::InterfaceNotify(DBTablePartBase *part,
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
bool VnswInterfaceListener::IsInterfaceActive(const HostInterfaceEntry *entry) {
    return entry->oper_seen_ && entry->host_seen_ && entry->link_up_;
}

static void InterfaceResync(Agent *agent, uint32_t id, bool active) {
    InterfaceTable *table = agent->GetInterfaceTable();
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

VnswInterfaceListener::HostInterfaceEntry *
VnswInterfaceListener::GetHostInterfaceEntry(const std::string &name) {
    HostInterfaceTable::iterator it = host_interface_table_.find(name);
    if (it == host_interface_table_.end())
        return NULL;

    return it->second;
}

// Handle transition from In-Active to Active interface
void VnswInterfaceListener::Activate(const std::string &name, uint32_t id) {
    if (name == agent_->vhost_interface_name()) {
        // link-local routes would have been deleted when vhost link was down.
        // Add all routes again on activation
        AddLinkLocalRoutes();
    }
    InterfaceResync(agent_, id, true);
}

void VnswInterfaceListener::DeActivate(const std::string &name, uint32_t id){
    InterfaceResync(agent_, id, false);
}

void VnswInterfaceListener::SetSeen(const std::string &name, bool oper) {
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

void VnswInterfaceListener::ResetSeen(const std::string &name, bool oper) {
    HostInterfaceEntry *entry = GetHostInterfaceEntry(name);
    if (entry == NULL)
        return;

    bool old_active = IsInterfaceActive(entry);
    if (oper) {
        entry->oper_seen_ = false;
    } else {
        entry->host_seen_ = false;
    }

    if (old_active == IsInterfaceActive(entry))
        return;

    if (old_active)
        DeActivate(name, entry->oper_id_);

    if (entry->oper_seen_ == false && entry->host_seen_ == false) {
        HostInterfaceTable::iterator it = host_interface_table_.find(name);
        host_interface_table_.erase(it);
        delete entry;
    }
}

void VnswInterfaceListener::SetLinkState(const std::string &name, bool link_up){
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

void VnswInterfaceListener::HandleInterfaceEvent(const Event *event) {
    if (event->event_ == Event::DEL_INTERFACE) {
        ResetSeen(event->interface_, false);
    } else {
        SetSeen(event->interface_, false);
        bool up = ((event->flags_ & (IFF_UP | IFF_RUNNING)) == 
                   (IFF_UP | IFF_RUNNING));
        SetLinkState(event->interface_, up);

        // In XEN mode, notify add of XAPI interface
        if (agent_->isXenMode()) {
            if (string::npos != event->interface_.find(XAPI_INTF_PREFIX)) {
                agent_->params()->set_xen_ll_name(event->interface_);
                agent_->init()->InitXenLinkLocalIntf();
            }
        }
    }
}

bool VnswInterfaceListener::IsValidLinkLocalAddress(const Ip4Address &addr) 
    const {
    return (ll_addr_table_.find(addr) != ll_addr_table_.end());
}

/****************************************************************************
 * Interface Address event handler
 ****************************************************************************/
void VnswInterfaceListener::SetAddress(const Event *event) {
    HostInterfaceEntry *entry = GetHostInterfaceEntry(event->interface_);
    if (entry == NULL) {
        entry = new HostInterfaceEntry();
        host_interface_table_.insert(make_pair(event->interface_, entry));
    }

    entry->addr_ = event->addr_;
    entry->plen_ = event->plen_;
}

void VnswInterfaceListener::ResetAddress(const Event *event) {
    HostInterfaceEntry *entry = GetHostInterfaceEntry(event->interface_);
    if (entry == NULL) {
        return;
    }

    entry->addr_ = event->addr_;
    entry->plen_ = event->plen_;
}

void VnswInterfaceListener::HandleAddressEvent(const Event *event) {
    // Update address in interface table
    if (event->event_ == Event::DEL_ADDR) {
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
    if (agent_->GetRouterIdConfigured() == false)
        dep_init_reqd = true;

    // Update vhost ip-address and enqueue db request
    agent_->SetRouterId(event->addr_);
    agent_->SetPrefixLen(event->plen_);
    InetInterface::CreateReq(agent_->GetInterfaceTable(),
                             agent_->vhost_interface_name(),
                             InetInterface::VHOST, agent_->GetDefaultVrf(),
                             event->addr_, event->plen_, agent_->GetGatewayId(),
                             agent_->GetDefaultVrf());
    if (dep_init_reqd)
        RouterIdDepInit(agent_);
}

/****************************************************************************
 * Link Local route event handler
 ****************************************************************************/
static int AddAttr(uint8_t *buff, int type, void *data, int alen) {
    struct nlmsghdr *n = (struct nlmsghdr *)buff;
    int len = RTA_LENGTH(alen);

    if (NLMSG_ALIGN(n->nlmsg_len) + len > VnswInterfaceListener::kMaxBufferSize)
        return -1;

    struct rtattr *rta = (struct rtattr*)(((char*)n)+NLMSG_ALIGN(n->nlmsg_len));
    rta->rta_type = type;
    rta->rta_len = len;
    memcpy(RTA_DATA(rta), data, alen);
    n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len) + len;
    return 0;
}

void VnswInterfaceListener::UpdateLinkLocalRoute(const Ip4Address &addr,
                                                 bool del_rt) {
    struct nlmsghdr *nlh;
    struct rtmsg *rtm;
    uint32_t ipaddr;

    netlink_msg_tx_count_++;
    if (agent_->test_mode())
        return;

    memset(tx_buf_, 0, kMaxBufferSize);

    nlh = (struct nlmsghdr *) tx_buf_;
    rtm = (struct rtmsg *) NLMSG_DATA (nlh);

    /* Fill in the nlmsg header*/
    nlh->nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
    if (del_rt) {
        nlh->nlmsg_type = RTM_DELROUTE;
        nlh->nlmsg_flags = NLM_F_REQUEST;
    } else {
        nlh->nlmsg_type = RTM_NEWROUTE;
        nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE;
    }
    nlh->nlmsg_seq = ++seqno_;
    rtm = (struct rtmsg *) NLMSG_DATA (nlh);
    rtm->rtm_table = RT_TABLE_MAIN;
    rtm->rtm_family = AF_INET;
    rtm->rtm_type = RTN_UNICAST;
    rtm->rtm_protocol = kVnswRtmProto;
    rtm->rtm_scope = RT_SCOPE_LINK;
    rtm->rtm_dst_len = 32;
    ipaddr = RT_TABLE_MAIN;
    AddAttr(tx_buf_, RTA_TABLE, (void *) &ipaddr, 4);
    ipaddr = htonl(addr.to_ulong());
    AddAttr(tx_buf_, RTA_DST, (void *) &ipaddr, 4);
    int if_index = if_nametoindex(agent_->vhost_interface_name().c_str());
    AddAttr(tx_buf_, RTA_OIF, (void *) &if_index, 4);

    boost::system::error_code ec;
    sock_.send(boost::asio::buffer(nlh,nlh->nlmsg_len), 0, ec);
    assert(ec.value() == 0);
}

// Handle link-local route changes resulting from ADD_LL_ROUTE or DEL_LL_ROUTE
void VnswInterfaceListener::LinkLocalRouteFromLinkLocalEvent(Event *event) {
    if (event->event_ == Event::DEL_LL_ROUTE) {
        ll_addr_table_.erase(event->addr_);
        UpdateLinkLocalRoute(event->addr_, true); } else {
        ll_addr_table_.insert(event->addr_);
        UpdateLinkLocalRoute(event->addr_, false);
    }
}

// For link-local routes added by agent, we treat agent as master. So, force 
// add the routes again if they are deleted from kernel
void VnswInterfaceListener::LinkLocalRouteFromRouteEvent(Event *event) {
    if (event->protocol_ != kVnswRtmProto)
        return;

    if (ll_addr_table_.find(event->addr_) == ll_addr_table_.end())
        return;

    if ((event->event_ == Event::DEL_ROUTE) ||
        (event->event_ == Event::ADD_ROUTE 
         && event->interface_ != agent_->vhost_interface_name())) {
        UpdateLinkLocalRoute(event->addr_, false);
    }
}

void VnswInterfaceListener::AddLinkLocalRoutes() {
    for (LinkLocalAddressTable::iterator it = ll_addr_table_.begin();
         it != ll_addr_table_.end(); ++it) {
        UpdateLinkLocalRoute(*it, false);
    }
}

void VnswInterfaceListener::DelLinkLocalRoutes() {
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

bool VnswInterfaceListener::ProcessEvent(Event *event) {
    LOG(DEBUG, "VnswInterfaceListener Event " << EventTypeToString(event->event_) 
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

/****************************************************************************
 * Netlink message handlers
 * Decodes netlink messages and enqueues events to revent_queue_
 ****************************************************************************/
static string NetlinkTypeToString(uint32_t type) {
    switch (type) {
    case NLMSG_DONE:
        return "NLMSG_DONE";
    case RTM_NEWADDR:
        return "RTM_NEWADDR";
    case RTM_DELADDR:
        return "RTM_DELADDR";
    case RTM_NEWROUTE:
        return "RTM_NEWROUTE";
    case RTM_DELROUTE:
        return "RTM_DELROUTE";
    case RTM_NEWLINK:
        return "RTM_NEWLINK";
    case RTM_DELLINK:
        return "RTM_DELLINK";
    default:
        break;
    }

    std::stringstream str;
    str << "UNHANDLED <" << type << ">";
    return str.str();
}

static VnswInterfaceListener::Event *HandleNetlinkRouteMsg(struct nlmsghdr *nlh)
{
    struct rtmsg *rtm = (struct rtmsg *) NLMSG_DATA (nlh);

    if (rtm->rtm_family != AF_INET || rtm->rtm_table != RT_TABLE_MAIN 
        || rtm->rtm_type != RTN_UNICAST || rtm->rtm_scope != RT_SCOPE_LINK) {
        LOG(DEBUG, "Ignoring Netlink route with family "
            << (uint32_t)rtm->rtm_family
            << " table " << (uint32_t)rtm->rtm_table
            << " type " << (uint32_t)rtm->rtm_type
            << " scope " << (uint32_t)rtm->rtm_family);
        return NULL;
    }

    int oif = -1;
    uint32_t dst_ip = 0;
    uint32_t gw_ip = 0;

    /* Get the route atttibutes len */
    int rtl = RTM_PAYLOAD(nlh);

    /* Loop through all attributes */
    for (struct rtattr *rth = (struct rtattr *) RTM_RTA(rtm);
         RTA_OK(rth, rtl); rth = RTA_NEXT(rth, rtl)) {
        /* Get the gateway (Next hop) */
        if (rth->rta_type == RTA_DST) {
            dst_ip = *((int *)RTA_DATA(rth));
        }
        if (rth->rta_type == RTA_GATEWAY) {
             gw_ip = *((int *)RTA_DATA(rth));
        }
        if (rth->rta_type == RTA_OIF) {
             oif = *((int *)RTA_DATA(rth));
        }
    }

    if (oif == -1) {
        return NULL;
    }

    char name[IFNAMSIZ];
    if_indextoname(oif, name);
    Ip4Address dst_addr((unsigned long)ntohl(dst_ip));
    Ip4Address gw_addr((unsigned long)ntohl(gw_ip));
    LOG(DEBUG, "Handle netlink route message " 
        << NetlinkTypeToString(nlh->nlmsg_type)
        << " : " << dst_addr.to_string() << "/" << rtm->rtm_dst_len
        << " Interface " << name << " GW " << gw_addr.to_string());

    VnswInterfaceListener::Event::Type type;
    if (nlh->nlmsg_type == RTM_DELROUTE) {
        type = VnswInterfaceListener::Event::DEL_ROUTE;
    } else {
        type = VnswInterfaceListener::Event::ADD_ROUTE;
    }

    return new VnswInterfaceListener::Event(type, dst_addr, rtm->rtm_dst_len,
                                            name, gw_addr, rtm->rtm_protocol,
                                            rtm->rtm_flags);
}

static VnswInterfaceListener::Event *HandleNetlinkIntfMsg(struct nlmsghdr *nlh){
    /* Get the atttibutes len */
    int rtl = RTM_PAYLOAD(nlh);

    const char *port_name = NULL;
    struct ifinfomsg *ifi = (struct ifinfomsg *) NLMSG_DATA (nlh);
    /* Loop through all attributes */
    for (struct rtattr *rth = IFLA_RTA(ifi); RTA_OK(rth, rtl);
         rth = RTA_NEXT(rth, rtl)) {
        /* Get the interface name */
        if (rth->rta_type == IFLA_IFNAME) {
            port_name = (char *) RTA_DATA(rth);
        }
    }

    assert(port_name != NULL);
    LOG(DEBUG, "Handle netlink interface message " 
        << NetlinkTypeToString(nlh->nlmsg_type) 
        << " for interface " << port_name);

    VnswInterfaceListener::Event::Type type;
    if (nlh->nlmsg_type == RTM_DELLINK) {
        type = VnswInterfaceListener::Event::DEL_INTERFACE;
    } else {
        type = VnswInterfaceListener::Event::ADD_INTERFACE;
    }
    return new VnswInterfaceListener::Event(type, port_name, ifi->ifi_flags);
}

static VnswInterfaceListener::Event *HandleNetlinkAddrMsg(struct nlmsghdr *nlh){
    struct ifaddrmsg *ifa = (struct ifaddrmsg *) NLMSG_DATA (nlh); 
 
    // Get interface name from os-index
    char name[IFNAMSIZ];
    if_indextoname(ifa->ifa_index, name);

    LOG(DEBUG, "Handle netlink address message " 
        << NetlinkTypeToString(nlh->nlmsg_type) << " for interface " << name);

    uint32_t ipaddr = 0;
    int rtl = IFA_PAYLOAD(nlh);
    for (struct rtattr *rth = IFA_RTA(ifa); rtl && RTA_OK(rth, rtl);
         rth = RTA_NEXT(rth,rtl)) {
        if (rth->rta_type != IFA_LOCAL) {
            continue;
        }

        ipaddr = ntohl(* ((uint32_t *)RTA_DATA(rth)));
    }

    if (ipaddr == 0)
        return NULL;

    assert(ipaddr != 0);
    VnswInterfaceListener::Event::Type type;
    if (nlh->nlmsg_type == RTM_DELADDR) {
        type = VnswInterfaceListener::Event::DEL_ADDR;
    } else {
        type = VnswInterfaceListener::Event::ADD_ADDR;
    }
    return new VnswInterfaceListener::Event(type, name, Ip4Address(ipaddr),
                                            ifa->ifa_prefixlen, ifa->ifa_flags);
}

int VnswInterfaceListener::NlMsgDecode(struct nlmsghdr *nl, std::size_t len, 
                                       uint32_t seq_no) {
    Event *event = NULL;
    struct nlmsghdr *nlh = nl;
    for (; (NLMSG_OK(nlh, len)); nlh = NLMSG_NEXT(nlh, len)) {

        switch (nlh->nlmsg_type) {
        case NLMSG_DONE:
            if (nlh->nlmsg_seq == seq_no) {
                return 1;
            }
            return 0;
        case RTM_NEWADDR:
        case RTM_DELADDR:
            event = HandleNetlinkAddrMsg(nlh);
            break;
        case RTM_NEWROUTE:
        case RTM_DELROUTE:
            event = HandleNetlinkRouteMsg(nlh);
            break;
        case RTM_NEWLINK:
        case RTM_DELLINK:
            event = HandleNetlinkIntfMsg(nlh);
            break;
        default:
            LOG(DEBUG, "VnswInterfaceListener got message : " 
                << NetlinkTypeToString(nlh->nlmsg_type));
            break;
        }

        if (event) {
            revent_queue_->Enqueue(event);
        }
    }

    return 0;
}
