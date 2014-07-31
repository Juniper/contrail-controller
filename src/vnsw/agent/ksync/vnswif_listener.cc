/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#if defined(__linux__)
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#elif defined(__FreeBSD__)
#include <ifaddrs.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <strings.h>
/* net/route.h includes net/radix .h that defines Free macro.
   Definition collides with ksync includes */
#if defined(Free)
#undef Free
#endif
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#endif
#include <net/if.h>
#include <sys/ioctl.h>
#include <net/address.h>

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

#if defined(__FreeBSD__)
/* Pointers are ordered in the same way as address flags (RTA_*)
 * from route.h */
struct rt_addresses {
    struct sockaddr *dst;
    struct sockaddr *gw;
    struct sockaddr *netmask;
    struct sockaddr *genmask;
    struct sockaddr *ifp;
    struct sockaddr *ifa;
    struct sockaddr *author;
    struct sockaddr *brd;
};

struct rt_msghdr_common {
    u_short rtmc_msglen;
    u_char  rtmc_version;
    u_char  rtmc_type;
};

#define RTM_ADDR_MAX ((int)(sizeof(struct rt_addresses)/ \
                      sizeof(struct sockaddr *)))

int VnswInterfaceListener::NetmaskLen(int mask)
{
    if (mask == 0)
        return 0;

    return 33 - ffs(mask);
}

unsigned int
VnswInterfaceListener::RTMGetAddresses(
    const char *in, size_t *size, unsigned int af,
    struct rt_addresses *rta)
{
    struct sockaddr **out = (struct sockaddr **)rta;
    int i = 0;
    size_t wsize = *size;
    unsigned int oaf = af;

    if (!(af & ((unsigned int)-1 >>  (32 - RTM_ADDR_MAX))))
        return (af & (unsigned int)-1 << RTM_ADDR_MAX);

    while (i < (int)RTM_ADDR_MAX && af != 0 && wsize != 0) {
        if (wsize < SA_SIZE(in)) {
            break;
        }

        if (af & 1) {
            *out = (struct sockaddr *)in;
            wsize -= SA_SIZE(in);
            in += SA_SIZE(in);
        }
        i++;
        out++;
        af >>= 1;
    }
    oaf &= (unsigned int)-1 << i;

    *size -= wsize;

    return oaf;
}


const string
VnswInterfaceListener::RTMTypeToString(int type)
{
    static const char *types[] = {
        "RTM_ADD",
        "RTM_DELETE",
        "RTM_CHANGE",
        "RTM_GET",
        "RTM_LOSING",
        "RTM_REDIRECT",
        "RTM_MISS",
        "RTM_LOCK",
        "RTM_OLDADD",
        "RTM_OLDDEL",
        "RTM_RESOLVE",
        "RTM_NEWADDR",
        "RTM_DELADDR",
        "RTM_IFINFO",
        "RTM_NEWMADDR",
        "RTM_DELMADDR",
        "RTM_IFANNOUNCE",
        "RTM_IEEE80211"
    };

    if (type > 0 && (unsigned long)type < sizeof(types)/sizeof(types[0]))
        return types[type-1];

    std::stringstream str;
    str << "UNHANDLED <" << type << ">";
    return str.str();
}

VnswInterfaceListener::Event *
VnswInterfaceListener::RTMProcess(const struct rt_msghdr *rtm, size_t size)
{
    struct rt_addresses rta = { 0 };
    size_t s = size - sizeof(*rtm);
    char name[IFNAMSIZ] = { 0 };
    Event::Type type;
    unsigned int mask_len = 0;
    struct sockaddr_in *t;

    /* This actually is an error */
    if (sizeof(*rtm) > size) {
        LOG(ERROR, __PRETTY_FUNCTION__ << ": message too short");
        return NULL;
    }

    /* Not interested in self generated messages */
    if (rtm->rtm_pid == pid_)
        return NULL;

    /* Special case: RTM_DELETE may come without gateway and interface
       index. Do not serve that, event requires if name na gateway
       that will not be provided */
    if (rtm->rtm_type == RTM_DELETE &&
        !(rtm->rtm_addrs & RTA_GATEWAY))
        return NULL;

    if (RTMGetAddresses((char *)(rtm + 1) , &s,
        rtm->rtm_addrs, &rta) != 0) {
        LOG(ERROR, __PRETTY_FUNCTION__
            << RTMTypeToString(rtm->rtm_type)
            << "IP address extraction failed.");
        return NULL;
    }

    if (rtm->rtm_addrs & RTA_NETMASK)
    {
        mask_len = NetmaskLen(
            ntohl(((struct sockaddr_in *)rta.netmask)->sin_addr.s_addr)
        );
    }

    /* Only routes to host are supported, which means routes that
       either have RTF_HOST flags or netmask narrowed to one host
       (unicast, all 1s) */
    if (!(rtm->rtm_flags & RTF_HOST || mask_len == 32))
    {
        LOG(DEBUG, __PRETTY_FUNCTION__ << ": "
            << RTMTypeToString(rtm->rtm_type)
            << " misses requirements for processing");

        return NULL;
    }

    /* There might be no interface name within address block, but there
       is always interface index that can be used to get the name */
    if (rtm->rtm_addrs & RTA_IFP && rta.ifp != NULL) {
        struct sockaddr_dl *sd = (struct sockaddr_dl *)rta.ifp;
        strncpy(name, sd->sdl_data, sd->sdl_nlen);
    } else {
        if_indextoname(rtm->rtm_index, name);
    }

    t = (struct sockaddr_in *)rta.dst;
    Ip4Address dst_addr((unsigned long)ntohl(t->sin_addr.s_addr));
    t = (struct sockaddr_in *)rta.gw;
    Ip4Address gw_addr((unsigned long)ntohl(t->sin_addr.s_addr));

    if (rtm->rtm_type == RTM_DELETE) {
        type = Event::DEL_ROUTE;
    } else {
        /* Handles also RTM_GET message, which is similar in structure
           but is used for reporting current state */
        type = Event::ADD_ROUTE;
    }

    /* "Protocol" the route has been set with */
    char proto = rtm->rtm_flags & (RTF_STATIC | RTF_DYNAMIC);

    LOG(DEBUG, __PRETTY_FUNCTION__ << ": "
        << RTMTypeToString(rtm->rtm_type)
        << " : " << dst_addr.to_string() << "/" << mask_len
        << " Interface " << name << " GW " << gw_addr.to_string());

    return new Event(type, dst_addr, mask_len, name, gw_addr,
                     proto, rtm->rtm_flags);
}

VnswInterfaceListener::Event *
VnswInterfaceListener::RTMProcess(const struct ifa_msghdr *rtm, size_t size)
{
    struct rt_addresses rta = { 0 };
    size_t s = size;
    char name[IFNAMSIZ] = { 0 };
    struct sockaddr_in *t;
    Event::Type type;

    /* This actually is an error */
    if (sizeof(*rtm) > size) {
        LOG(ERROR, __PRETTY_FUNCTION__ << ": message too short");
        return NULL;
    }

    if (RTMGetAddresses((char *)(rtm + 1) , &s,
        rtm->ifam_addrs, &rta) != 0) {
        LOG(ERROR, __PRETTY_FUNCTION__ << ": message "
            << RTMTypeToString(rtm->ifam_type)
            << "IP address extraction failed.");
        return NULL;
    }

    assert(rta.ifa != NULL);

    if (rtm->ifam_addrs & RTA_IFP && rta.ifp != NULL) {
        struct sockaddr_dl *sd = (struct sockaddr_dl *)rta.ifp;
        strncpy(name, sd->sdl_data, sd->sdl_nlen);
    } else {
        if_indextoname(rtm->ifam_index, name);
    }

    if (rtm->ifam_type == RTM_DELADDR) {
        type = Event::DEL_ADDR;
    } else {
        /* RTM_NEWADDR is only other command */
        type = Event::ADD_ADDR;
    }

    t = (struct sockaddr_in *)rta.ifa;
    Ip4Address addr((unsigned long)ntohl(t->sin_addr.s_addr));

    LOG(DEBUG, __PRETTY_FUNCTION__ << ": message "
        << RTMTypeToString(rtm->ifam_type)
        << " Interface " << name << " IP " << addr.to_string());

    return new Event(type, name, addr);
}

VnswInterfaceListener::Event *
VnswInterfaceListener::RTMProcess(
    const struct if_msghdr *rtm, size_t size)
{
    struct rt_addresses rta = { 0 };
    size_t s = size;
    char name[IFNAMSIZ] = { 0 };
    Event::Type type;

    /* This actually is an error */
    if (sizeof(*rtm) > size) {
        LOG(ERROR, __PRETTY_FUNCTION__ << ": message too short");
        return NULL;
    }

    if (RTMGetAddresses((char *)(rtm + 1) , &s,
        rtm->ifm_addrs, &rta) != 0) {
        LOG(ERROR, __PRETTY_FUNCTION__ << ": message "
            << RTMTypeToString(rtm->ifm_type)
            << "IP address extraction failed.");
        return NULL;
    }

    if (rtm->ifm_addrs & RTA_IFP && rta.ifp != NULL) {
        struct sockaddr_dl *sd = (struct sockaddr_dl *)rta.ifp;
        strncpy(name, sd->sdl_data, sd->sdl_nlen);
    } else {
        if_indextoname(rtm->ifm_index, name);
    }

    if (rtm->ifm_flags & RTF_UP) {
        type = Event::ADD_INTERFACE;
    } else {
        type = Event::DEL_INTERFACE;
    }

    LOG(DEBUG, __PRETTY_FUNCTION__ << ": message "
        << RTMTypeToString(rtm->ifm_type)
        << " Interface " << name);

    return new Event(type, name, rtm->ifm_flags);
}

int VnswInterfaceListener::RTMDecode(const struct rt_msghdr_common *rtm,
    size_t len, uint32_t seq_no)
{
    Event *event = NULL;
    /* Segfault protection */
    if (len < sizeof(*rtm)) {
        return 1;
    }

    switch (rtm->rtmc_type) {
    case RTM_GET:
    case RTM_ADD:
    case RTM_DELETE:
        event = RTMProcess((const struct rt_msghdr *)rtm, len);
        break;
    case RTM_NEWADDR:
    case RTM_DELADDR:
        event = RTMProcess((const struct ifa_msghdr *)rtm, len);
        break;
    case RTM_IFINFO:
        event = RTMProcess((const struct if_msghdr *)rtm, len);
        break;
    default:
        LOG(DEBUG, __PRETTY_FUNCTION__ << ": message "
                << RTMTypeToString(rtm->rtmc_type));
        break;
    }

    if (event != NULL) {
        revent_queue_->Enqueue(event);
    }
    return 0;
}

int VnswInterfaceListener::RTMProcessBuffer(const void *buffer, size_t size)
{
    struct rt_msghdr_common *p = (struct rt_msghdr_common *)buffer;
    int ret = 0;

    while (size != 0 && size >= p->rtmc_msglen && p->rtmc_msglen > 0) {
        ret = RTMDecode(p, size, 0);

        if (ret)
            break;

        size -= p->rtmc_msglen;
        p = (struct rt_msghdr_common *)((char *)p + p->rtmc_msglen);
    }

    /* This is not an error since size of buffer, instead of size of
       all messages in that buffer could be given. */
    if (size > 0)
        LOG(DEBUG, __PRETTY_FUNCTION__ 
            << ": message data left unprocessed(" << size << " bytes)");

    return ret;
}

int VnswInterfaceListener::Getfib()
{
    int fibnum;
    size_t size = sizeof(fibnum);

    if (sysctlbyname("net.fibs", &fibnum, &size, NULL, 0) != 0)
        return -1;

    size = sizeof(fibnum);

    if (fibnum >= 0 &&
        sysctlbyname("net.my_fibnum", &fibnum, &size, NULL, 0) != -1)
    {
        return fibnum;
    }
    return -2;
}

int VnswInterfaceListener::RTCreateSocket(int fib)
{
    /* Interested in link layer and inet messages, so can not
       specify one family. Filtering will have to be done by
       hand. */
    int s = socket(PF_ROUTE, SOCK_RAW, AF_UNSPEC);

    if (s == -1) {
        LOG(ERROR, __PRETTY_FUNCTION__ << ": failed to create "
            << "socket with error: " << errno);
        assert(0);
    }

    if (setsockopt(s, SOL_SOCKET, SO_SETFIB, (void *)&fib,
        sizeof(fib)) == -1)
    {
        close(s);
        LOG(ERROR, __PRETTY_FUNCTION__ << " failed to setup "
            << "socket with error: " << errno);
        assert(0);
    }

    /* Assign native socket to boost asio */
    boost::asio::local::datagram_protocol protocol;
    sock_.assign(protocol, s);

    return s;
}

void *VnswInterfaceListener::SysctlDump(int *mib, int mib_len, size_t *ret_len, int *ret_code)
{
    size_t size = 0;
    void *dump_buf = NULL;
    int ret = 0;

    if (sysctl(mib, mib_len, NULL, &size, NULL, 0)) {
        ret = 1;
        goto exit_here;
    }

    dump_buf = calloc(size, 1);
    if (dump_buf == NULL) {
        ret = 2;
        goto exit_here;
    }

    if (sysctl(mib, mib_len, dump_buf, &size, NULL, 0)) {
        ret = 3;
        free(dump_buf);
        size = 0;
        dump_buf = NULL;
        goto exit_here;
    }

exit_here:
    *ret_code = ret;
    *ret_len = size;
    return dump_buf;
}

int VnswInterfaceListener::RTInitRoutes(int fib)
{
    int mib[] = { CTL_NET, PF_ROUTE, 0, AF_INET, NET_RT_DUMP, 0, fib};
    int mib_len = sizeof(mib)/sizeof(int);
    size_t size;
    int ret;
    void *p;

    /* If fib is -1 then we will force default fib, this is equivalent
       to providing mib with five ints only (as 6th is None and
       7th is fib number - or 0 if not provided). See man 3 sysctl,
       PF_ROUTE. */
    if (fib < 0)
        mib_len -= 2;

    p = SysctlDump(mib, mib_len, &size, &ret);

    if (p != NULL) {
        ret = RTMProcessBuffer(p, size);
        free(p);
    }

    return ret;
}

int VnswInterfaceListener::RTInitIfAndAddr()
{
    int mib[] = { CTL_NET, PF_ROUTE, 0, AF_INET, NET_RT_IFLIST, 0};
    int mib_len = sizeof(mib)/sizeof(int);
    size_t size;
    int ret;
    void *p;

    p = SysctlDump(mib, mib_len, &size, &ret);

    if (p != NULL) {
        ret = RTMProcessBuffer(p, size);
        free(p);
    }

    return ret;
}
#endif

VnswInterfaceListener::VnswInterfaceListener(Agent *agent) : 
    agent_(agent), read_buf_(NULL), sock_fd_(0), 
    sock_(*(agent->event_manager())->io_service()),
    intf_listener_id_(DBTableBase::kInvalidId), seqno_(0),
    vhost_intf_up_(false), ll_addr_table_(), revent_queue_(NULL),
    netlink_ll_add_count_(0), netlink_ll_del_count_(0), vhost_update_count_(0) { 
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
    intf_listener_id_ = agent_->interface_table()->Register
        (boost::bind(&VnswInterfaceListener::InterfaceNotify, this, _1, _2));

    /* Allocate Route Event Workqueue */
    revent_queue_ = new WorkQueue<Event *>
                    (TaskScheduler::GetInstance()->GetTaskId("db::DBTable"), 0,
                     boost::bind(&VnswInterfaceListener::ProcessEvent, 
                                 this, _1));

    if (agent_->test_mode())
        return;

#if defined(__linux__)
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

#elif defined(__FreeBSD__)
    /* Get routing table */
    int fib = Getfib();

    if (fib < 0) {
        LOG(ERROR, __PRETTY_FUNCTION__ << ": Failed to get fib. "
            << "Expected >= 0, obtained " << fib);
        assert(fib >=0);
    }

    pid_ = getpid();

    sock_fd_ = RTCreateSocket(fib);

    /* Fetch interfaces and IPv4 addresses */
    RTInitIfAndAddr();

    /* Fetch routes */
    RTInitRoutes(fib);
#else
#error "Unsupported platform"
#endif
    RegisterAsyncHandler();
}

void VnswInterfaceListener::Shutdown() { 
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

// Create netlink socket
#if defined(__linux__)
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
#endif

void VnswInterfaceListener::RegisterAsyncHandler() {
    read_buf_ = new uint8_t[kMaxBufferSize];
    sock_.async_receive(boost::asio::buffer(read_buf_, kMaxBufferSize), 
                        boost::bind(&VnswInterfaceListener::ReadHandler, this,
                                 boost::asio::placeholders::error,
                                 boost::asio::placeholders::bytes_transferred));
}

void VnswInterfaceListener::ReadHandler(const boost::system::error_code &error,
                                 std::size_t len) {
#if defined(__linux__)
    struct nlmsghdr *nlh;

    if (error == 0) {
        nlh = (struct nlmsghdr *)read_buf_;
        NlMsgDecode(nlh, len, -1);
    } else {
        LOG(ERROR, "Error < : " << error.message() << 
                   "> reading packet on netlink sock");
    }
#elif defined(__FreeBSD__)
    if (error == 0) {
        RTMDecode((struct rt_msghdr_common *)read_buf_, len, -1);
    } else {
        LOG(ERROR, "Error < : " << error.message() <<
                   "> reading packet on PF_ROUTE sock");
    }
#endif

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
#if defined(__linux__)
        bool up = ((event->flags_ & (IFF_UP | IFF_RUNNING)) == 
                   (IFF_UP | IFF_RUNNING));
#elif defined(__FreeBSD__)
        bool up = (event->flags_ & RTF_UP);
#else
#error "Unsupported platform"
#endif
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

    entry->addr_ = Ip4Address(0);
    entry->plen_ = 0;
}

void VnswInterfaceListener::HandleAddressEvent(const Event *event) {
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

/****************************************************************************
 * Link Local route event handler
 ****************************************************************************/
#if defined(__linux__)
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
#endif

void VnswInterfaceListener::UpdateLinkLocalRoute(const Ip4Address &addr,
                                                 bool del_rt) {
#if defined(__linux__)
    struct nlmsghdr *nlh;
    struct rtmsg *rtm;
    uint32_t ipaddr;
#endif

    if (del_rt)
        netlink_ll_del_count_++;
    else
        netlink_ll_add_count_++;
    if (agent_->test_mode())
        return;

    memset(tx_buf_, 0, kMaxBufferSize);
#if defined(__linux__)

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
#elif defined(__FreeBSD__)
    size_t size;
    struct rt_msghdr *rtm = (struct rt_msghdr *)tx_buf_;
    struct sockaddr_in *si = (struct sockaddr_in *)(rtm + 1);
    struct sockaddr *gw = NULL;
    struct ifaddrs *ifap, *oifap;
    const char *vhost_name = agent_->vhost_interface_name().c_str();

    /* Need to get the gateway IP, the interface is not enough */
    if (getifaddrs(&ifap) == -1) {
        /* We will not be able to set route if we are not able to
           set gateway. That's some major failure */
        LOG(ERROR, __PRETTY_FUNCTION__ 
            << ": failed to get list of interfaces");
            assert(0);
    }

    oifap = ifap;

    while (ifap != NULL) {
        if (ifap->ifa_name != NULL &&
            !strncmp(ifap->ifa_name, vhost_name, IFNAMSIZ) &&
            SA_SIZE(ifap->ifa_addr) >= SA_SIZE(NULL) &&
            ifap->ifa_addr->sa_family == AF_INET)
        {
            gw = ifap->ifa_addr;
        }

        ifap = ifap->ifa_next;
    }

    if (gw == NULL) {
        /* We will not be able to set route if we are not able to
           set gateway. That's some major failure */
        LOG(ERROR, __PRETTY_FUNCTION__ << ": failed to get IP for "
            << string(vhost_name));
            assert(gw != NULL);
    }


    /* Destination, Gateway and header */
    size = 2 * sizeof(struct sockaddr_in) + sizeof(struct rt_msghdr);

    if (size > kMaxBufferSize) {
        LOG(ERROR, __PRETTY_FUNCTION__
             << ": tx_buf_ to short, expected size " << size);
        assert(size < kMaxBufferSize);
    }

    rtm->rtm_msglen = size;
    rtm->rtm_version = RTM_VERSION;

    if (!del_rt)
        rtm->rtm_type = RTM_ADD;
    else
        rtm->rtm_type = RTM_DELETE;

    rtm->rtm_flags = RTF_UP|RTF_GATEWAY|RTF_STATIC|RTF_HOST;

    rtm->rtm_index = if_nametoindex(vhost_name);

    if (rtm->rtm_index == 0) {
        LOG(ERROR, __PRETTY_FUNCTION__ << ": Failed to obtain index "
            << "for interface " << vhost_name);
        assert(0);
    }

    rtm->rtm_addrs = RTA_DST|RTA_GATEWAY;
    rtm->rtm_pid = pid_;
    rtm->rtm_seq = seqno_++;

    /* Destination */
    si->sin_len = sizeof(struct sockaddr_in);
    si->sin_family = AF_INET;
    si->sin_port = 0;
    si->sin_addr.s_addr = htonl(addr.to_ulong());

    /* Gateway */
    si = (struct sockaddr_in *)((char *)si + sizeof(struct sockaddr_in));
    /* Very unlikely... but hard to detect if happens */
    if (sizeof(struct sockaddr_in) < SA_SIZE(gw)) {
        LOG(ERROR, __PRETTY_FUNCTION__
            << ": Size of gateway address returned by kernel is "
            << SA_SIZE(gw) << "which far too big for socaddr_in struct");
        assert(sizeof(struct sockaddr_in) >= SA_SIZE(gw));
    }
    memcpy(si, gw, SA_SIZE(gw));

    freeifaddrs(oifap);

    boost::system::error_code ec;
    sock_.send(boost::asio::buffer(tx_buf_, size), 0, ec);
    if (ec.value() != 0) {
        LOG(ERROR, __PRETTY_FUNCTION__
            << ": PF_ROUTE message " << RTMTypeToString(rtm->rtm_type)
            << " send failed");
        assert(ec.value() == 0);
    }
#endif
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

    if (ll_addr_table_.find(event->addr_) == ll_addr_table_.end()) {
        // link-local route is in kernel, but not in agent. This can happen
        // when agent has restarted and the link-local route is not valid
        // after agent restart.
        // Delete the route
        if (event->event_ == Event::ADD_ROUTE) {
            UpdateLinkLocalRoute(event->addr_, true);
        }
        return;
    }

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
#if defined(__linux__)
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
#endif
