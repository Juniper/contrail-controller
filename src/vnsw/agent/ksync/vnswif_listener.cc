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
#include <cmn/agent_cmn.h>
#include <init/agent_param.h>
#include <init/agent_init.h>
#include <cfg/cfg_init.h>
#include <oper/route_common.h>
#include <oper/mirror_table.h>
#include <ksync/ksync_index.h>
#include <ksync/interface_ksync.h>
#include <ksync/vnswif_listener.h>

extern void RouterIdDepInit();

VnswInterfaceListener::VnswInterfaceListener(Agent *agent) : 
    agent_(agent), sock_(*(agent_->GetEventManager())->io_service()),
    seqno_(0), vhost_intf_up_(false) {
}

VnswInterfaceListener::~VnswInterfaceListener() {
    if (read_buf_){
        delete [] read_buf_;
        read_buf_ = NULL;
    }
    delete revent_queue_;
}

void VnswInterfaceListener::Init() {

    ifaddr_listen_ = !(agent_->params()->IsVHostConfigured());

    intf_listener_id_ = agent_->GetInterfaceTable()->Register
        (boost::bind(&VnswInterfaceListener::IntfNotify, this, _1, _2));

    /* Allocate Route Event Workqueue */
    revent_queue_ = new WorkQueue<VnswRouteEvent *>
                    (TaskScheduler::GetInstance()->GetTaskId("db::DBTable"), 0,
                     boost::bind(&VnswInterfaceListener::RouteEventProcess, 
                                 this, _1));

    /* Create socket and listen and handle ip address updates */
    CreateSocket();

    /* Read the IP from kernel and update the router-id 
     * only if it is not already configured (by reading from conf file)
     */
    if (ifaddr_listen_) {
        InitRouterId();
    }

    /* Fetch Links from kernel syncronously, to allow dump request for routes
     * to go through fine
     */
    InitFetchLinks();

    /* Fetch routes from kernel asyncronously and update the gateway-id */
    InitFetchRoutes();

    RegisterAsyncHandler();
}

int VnswInterfaceListener::AddAttr(int type, void *data, int alen) {
    struct nlmsghdr *n = (struct nlmsghdr *)tx_buf_;
    int len = RTA_LENGTH(alen);
    struct rtattr *rta;

    if (NLMSG_ALIGN(n->nlmsg_len) + len > max_buf_size)
        return -1;
    rta = (struct rtattr*)(((char*)n) + NLMSG_ALIGN(n->nlmsg_len));
    rta->rta_type = type;
    rta->rta_len = len;
    memcpy(RTA_DATA(rta), data, alen);
    n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len) + len;
    return 0;
}

void VnswInterfaceListener::IntfNotify(DBTablePartBase *part, DBEntryBase *e) {
    const VmInterface *vmport = dynamic_cast<VmInterface *>(e);
    if (vmport == NULL) {
        return;
    }

    DBState *s = e->GetState(part->parent(), intf_listener_id_);
    VnswIntfState *state = static_cast<VnswIntfState *>(s);
    VnswRouteEvent *re;

    if (vmport->IsDeleted() || !vmport->ipv4_active() ||
        !vmport->need_linklocal_ip()) {
        if (state) {
            re = new VnswRouteEvent(state->addr(), VnswRouteEvent::DEL_REQ);
            revent_queue_->Enqueue(re);
            e->ClearState(part->parent(), intf_listener_id_);
            delete state;
        }
        return;
    }

    if (!state && vmport->need_linklocal_ip()) {
        state = new VnswIntfState(vmport->mdata_ip_addr());
        e->SetState(part->parent(), intf_listener_id_, state);
        re = new VnswRouteEvent(state->addr(), VnswRouteEvent::ADD_REQ);
        revent_queue_->Enqueue(re);
    }
}

void VnswInterfaceListener::InitFetchLinks() {
    struct nlmsghdr *nlh;
    uint32_t seq_no;

    memset(tx_buf_, 0, max_buf_size);
    nlh = (struct nlmsghdr *)tx_buf_;

    /* Fill in the nlmsg header */

    // Length of message.
    nlh->nlmsg_len = NLMSG_LENGTH(sizeof(struct rtgenmsg));
    // Get links from kernel.
    nlh->nlmsg_type = RTM_GETLINK;

    // The message is a request for dump.
    nlh->nlmsg_flags = NLM_F_DUMP | NLM_F_REQUEST;
    // Sequence of the message packet.
    nlh->nlmsg_seq = ++seqno_;

    struct rtgenmsg *rt_gen = (struct rtgenmsg *) NLMSG_DATA (nlh);
    rt_gen->rtgen_family = AF_PACKET;

    seq_no = nlh->nlmsg_seq;
    boost::system::error_code ec;
    sock_.send(boost::asio::buffer(nlh,nlh->nlmsg_len), 0, ec);
    assert(ec.value() == 0);
    uint8_t read_buf[max_buf_size];

    /* Wait/Read the response for dump request, linux kernel doesn't handle 
     * dump request if response for previous dump request is not complete. 
     */
    int end = 0;
    while (end == 0) {
        memset(read_buf, 0, max_buf_size);
        std::size_t len = sock_.receive(boost::asio::buffer
                                      (read_buf, max_buf_size), 0, ec);
        assert(ec.value() == 0);
        struct nlmsghdr *nl = (struct nlmsghdr *)read_buf;
        end = NlMsgDecode(nl, len, seq_no);
    }
}
 
void VnswInterfaceListener::InitFetchRoutes() {
    struct nlmsghdr *nlh;

    memset(tx_buf_, 0, max_buf_size);
    nlh = (struct nlmsghdr *)tx_buf_;

    /* Fill in the nlmsg header */

    // Length of message.
    nlh->nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
    // Get the routes from kernel routing table
    nlh->nlmsg_type = RTM_GETROUTE;

    // The message is a request for dump.
    nlh->nlmsg_flags = NLM_F_DUMP | NLM_F_REQUEST;
    // Sequence of the message packet.
    nlh->nlmsg_seq = ++seqno_;

    boost::system::error_code ec;
    sock_.send(boost::asio::buffer(nlh,nlh->nlmsg_len), 0, ec);
    assert(ec.value() == 0);
}
 
void VnswInterfaceListener::KUpdateLinkLocalRoute(const Ip4Address &addr, 
                                           bool del_rt) {
    struct nlmsghdr *nlh;
    struct rtmsg *rtm;
    uint32_t ipaddr;

    memset(tx_buf_, 0, max_buf_size);

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
    AddAttr(RTA_TABLE, (void *) &ipaddr, 4);
    ipaddr = htonl(addr.to_ulong());
    AddAttr(RTA_DST, (void *) &ipaddr, 4);
    int if_index = if_nametoindex(agent_->vhost_interface_name().c_str());
    AddAttr(RTA_OIF, (void *) &if_index, 4);

    boost::system::error_code ec;
    sock_.send(boost::asio::buffer(nlh,nlh->nlmsg_len), 0, ec);
    assert(ec.value() == 0);
}

void VnswInterfaceListener::CreateVhostRoutes(Ip4Address &host_ip, 
                                              uint8_t plen) {
    Inet4UnicastAgentRouteTable *rt_table;

    std::string vrf_name = agent_->GetDefaultVrf();

    rt_table = static_cast<Inet4UnicastAgentRouteTable *>
        (agent_->GetVrfTable()->GetInet4UnicastRouteTable(vrf_name));
    if (rt_table == NULL) {
        assert(0);
    }

    if (agent_->GetPrefixLen() != 0) {
        Ip4Address old = agent_->GetRouterId();
        LOG(ERROR, "Host IP update not supported!");
        LOG(ERROR, "Old IP is " << old.to_string() << " prefix len "
                   << (unsigned short)agent_->GetPrefixLen());
        LOG(ERROR, "New IP is " << host_ip.to_string() << " prefix len "
                   << (unsigned short)plen);
        return;
    }
    LOG(DEBUG, "New IP is " << host_ip.to_string() << " prefix len "
               << (unsigned short)plen);
    agent_->SetRouterId(host_ip);
    agent_->SetPrefixLen(plen);

    InetInterface::CreateReq(agent_->GetInterfaceTable(),
                             agent_->vhost_interface_name(),
                             InetInterface::VHOST, agent_->GetDefaultVrf(),
                             host_ip, plen, agent_->GetGatewayId(),
                             agent_->GetDefaultVrf());
}

void VnswInterfaceListener::DeleteVhostRoutes(Ip4Address &host_ip, 
                                              uint8_t plen) {
    Inet4UnicastAgentRouteTable *rt_table;

    std::string vrf_name = agent_->GetDefaultVrf();

    rt_table = static_cast<Inet4UnicastAgentRouteTable *>
        (agent_->GetVrfTable()->GetInet4UnicastRouteTable(vrf_name));
    if (rt_table == NULL) {
        assert(0);
    }

    assert(agent_->GetPrefixLen() == plen && agent_->GetRouterId() == host_ip);

    LOG(DEBUG, "Delete VHost IP " << host_ip.to_string() << " prefix len "
                << (unsigned short)plen);
    rt_table->DeleteReq(agent_->GetLocalPeer(), vrf_name, host_ip, 32);
    rt_table->DelVHostSubnetRecvRoute(vrf_name, host_ip, plen);
    rt_table->DeleteReq(agent_->GetLocalPeer(), vrf_name, host_ip, plen);
    agent_->SetPrefixLen(0);
}

uint32_t VnswInterfaceListener::NetmaskToPrefix(uint32_t netmask) {
    uint32_t count = 0;

    while (netmask) {
        count++;
        netmask = (netmask - 1) & netmask;
    }
    return count;
}

uint32_t VnswInterfaceListener::FetchVhostAddress(bool netmask) {
    int fd;
    struct ifreq ifr;
    uint32_t addr;
    int req;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        LOG(ERROR, "Error <" << errno << ": " << strerror(errno) << 
            "> creating dgram socket");
        return 0;
    }

    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, agent_->vhost_interface_name().c_str(),
	    sizeof(ifr.ifr_name));

    if (netmask) {
        req = SIOCGIFNETMASK;
    } else {
        req = SIOCGIFADDR;
    }

    if (ioctl(fd, req, &ifr) < 0) {
        LOG(ERROR, "Error <" << errno << ": " << strerror(errno) << 
            "> getting IP address of " << ifr.ifr_name);
        return 0;
    }

    addr = ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr.s_addr;
    close(fd);
    return ntohl(addr);
}

void VnswInterfaceListener::InitRouterId() {

    uint32_t ipaddr, netmask, prefix;

    /* Fetch IP */
    ipaddr = FetchVhostAddress(false);
    if (!ipaddr) {
        return;
    }
    Ip4Address addr(ipaddr);

    /* Fetch Netmask */
    netmask = FetchVhostAddress(true);
    if (!netmask) {
        return;
    }
    prefix = NetmaskToPrefix(netmask);

    /* Create Vhost0 routes */
    CreateVhostRoutes(addr, prefix);
    LOG(DEBUG, "IP Address of vhost if is " << addr.to_string() << 
               " prefix " << prefix);
    LOG(DEBUG, "Netmask " << std::hex << netmask); 
}

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
    addr.nl_groups = (RTMGRP_IPV4_ROUTE | RTMGRP_LINK);
    if (ifaddr_listen_) {
        addr.nl_groups |= RTMGRP_IPV4_IFADDR;
    }
    if (bind(sock_fd_, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        LOG(ERROR, "Error <" << errno << ": " << strerror(errno) << 
                       "> binding to netlink address family");
        assert(0);
    }

    /* Assign native socket to boost asio */
    boost::asio::local::datagram_protocol protocol;
    sock_.assign(protocol, sock_fd_);
}

void VnswInterfaceListener::RegisterAsyncHandler() {
    read_buf_ = new uint8_t[max_buf_size];
    sock_.async_receive(boost::asio::buffer(read_buf_, max_buf_size), 
                        boost::bind(&VnswInterfaceListener::ReadHandler, this,
                                 boost::asio::placeholders::error,
                                 boost::asio::placeholders::bytes_transferred));
}

bool VnswInterfaceListener::RouteEventProcess(VnswRouteEvent *re) {
    bool route_found = (ll_addr_table_.find(re->addr_) != ll_addr_table_.end());
    switch (re->event_) {
    case VnswRouteEvent::ADD_REQ:
        ll_addr_table_.insert(re->addr_);
        KUpdateLinkLocalRoute(re->addr_, false);
        break;
    case VnswRouteEvent::DEL_REQ:
        ll_addr_table_.erase(re->addr_);
        KUpdateLinkLocalRoute(re->addr_, true);
        break;
    case VnswRouteEvent::ADD_RESP:
        if (!route_found) {
            KUpdateLinkLocalRoute(re->addr_, true);
        }
        break;
    case VnswRouteEvent::DEL_RESP:
        if (route_found) {
            KUpdateLinkLocalRoute(re->addr_, false);
        }
        break;
    case VnswRouteEvent::INTF_UP:
        if (vhost_intf_up_) {
            break;
        }
        /* Re-add the MetaData IP routes, which would have been removed
         * on interface flap.
         */
        vhost_intf_up_ = true;
        for (Ip4HostTableType::iterator it = ll_addr_table_.begin();
             it != ll_addr_table_.end(); ++it) {
            KUpdateLinkLocalRoute(*it, false);
        }
        break;
    case VnswRouteEvent::INTF_DOWN:
        vhost_intf_up_ = false;
        break;
    default:
        assert(0);
        break;
    }
    delete re;
    return true;
}

void VnswInterfaceListener::RouteHandler(struct nlmsghdr *nlh) {
    struct rtmsg *rtm;
    struct rtattr *rth;
    int rtl;
    uint32_t dst_ip = 0;

    rtm = (struct rtmsg *) NLMSG_DATA (nlh);
    if (rtm->rtm_family != AF_INET) {
        return;
    }
    if (rtm->rtm_table != RT_TABLE_MAIN) {
        return;
    }
    if (rtm->rtm_type != RTN_UNICAST) {
        return;
    }
    if (rtm->rtm_dst_len != 32 || rtm->rtm_scope != RT_SCOPE_LINK ||
        rtm->rtm_protocol != kVnswRtmProto) {
        return;
    }

    /* Get attributes of route_entry */
    rth = (struct rtattr *) RTM_RTA(rtm);
    /* Get the route atttibutes len */
    rtl = RTM_PAYLOAD(nlh);
    /* Loop through all attributes */
    for ( ; RTA_OK(rth, rtl); rth = RTA_NEXT(rth, rtl)) {
        /* Get the gateway (Next hop) */
        if (rth->rta_type == RTA_DST) {
            dst_ip = *((int *)RTA_DATA(rth));
            break;
        }
    }

    Ip4Address dst_addr((unsigned long)ntohl(dst_ip));
    VnswRouteEvent *re = NULL;
    switch (nlh->nlmsg_type) {
    case RTM_NEWROUTE:
        re = new VnswRouteEvent(dst_addr, VnswRouteEvent::ADD_RESP);
        break;
    case RTM_DELROUTE:
        re = new VnswRouteEvent(dst_addr, VnswRouteEvent::DEL_RESP);
        break;
    default:
        break;
    }
    if (re) {
        revent_queue_->Enqueue(re);
    }
}

/* XEN host specific interface event handler */
void VnswInterfaceListener::InterfaceHandler(struct nlmsghdr *nlh)
{
    struct ifinfomsg *rtm;
    struct rtattr *rth;
    int rtl;
    bool is_xapi_intf = false;
    bool is_vhost_intf = false;
    string port_name = "";

    rtm = (struct ifinfomsg *) NLMSG_DATA (nlh);
    /* Get the route atttibutes len */
    rtl = RTM_PAYLOAD(nlh);
    /* Loop through all attributes */
    for (rth = IFLA_RTA(rtm); RTA_OK(rth, rtl); rth = RTA_NEXT(rth, rtl)) {
        /* Get the interface name */
        if (rth->rta_type == IFLA_IFNAME) {
            port_name = (char *) RTA_DATA(rth);
            LOG(INFO, "port_name " << port_name);
            is_vhost_intf = (0 == 
                    agent_->vhost_interface_name().compare(port_name));
            /* required only for Xen Mode */
            if (agent_->isXenMode()) {
                is_xapi_intf = (string::npos != 
                                    port_name.find(XAPI_INTF_PREFIX));
            }
            break;
        }
    }

    if (is_vhost_intf && nlh->nlmsg_type == RTM_NEWLINK) {
        if ((rtm->ifi_flags & IFF_UP) != 0) {
            Ip4Address dst_addr((unsigned long)0);
            VnswRouteEvent *re = new VnswRouteEvent(dst_addr, 
                                                    VnswRouteEvent::INTF_UP);
            revent_queue_->Enqueue(re);
        } else {
            Ip4Address dst_addr((unsigned long)0);
            VnswRouteEvent *re = new VnswRouteEvent(dst_addr, 
                                                    VnswRouteEvent::INTF_DOWN);
            revent_queue_->Enqueue(re);
        }
    }

    if (is_xapi_intf) {
        switch (nlh->nlmsg_type) {
        case RTM_NEWLINK:
            LOG(INFO, "Received interface add for interface: " << port_name);
            agent_->params()->set_xen_ll_name(port_name);
            agent_->init()->InitXenLinkLocalIntf();
            break;
        case RTM_DELLINK:
            LOG(INFO, "Received interface delete for interface: " << port_name);
            break;
        }
    }
}

void VnswInterfaceListener::IfaddrHandler(struct nlmsghdr *nlh) {
    char name[IFNAMSIZ];
    uint32_t ipaddr;
    bool dep_init_reqd = false;
    struct rtattr *rth;
    int rtl;

    assert(ifaddr_listen_);
    struct ifaddrmsg *ifa = (struct ifaddrmsg *) NLMSG_DATA (nlh); 
 
    /* Ignore IP notification on non vnswif interfaces */
    if_indextoname(ifa->ifa_index, name);

    LOG(DEBUG, "Received change notification for " << name);
    if (agent_->vhost_interface_name().compare(name) != 0) {
        LOG(DEBUG, "Ignoring IP change notification received for "
                << name);
        return;
    }
    /* Ignore secondary IP notifications */
    if (ifa->ifa_flags & IFA_F_SECONDARY) {
        return;
    }
 
    rth = IFA_RTA(ifa);
    rtl = IFA_PAYLOAD(nlh);
    for (;rtl && RTA_OK(rth, rtl); rth = RTA_NEXT(rth,rtl))
    {
        if (rth->rta_type != IFA_LOCAL) {
            continue;
        }
        ipaddr = * ((uint32_t *)RTA_DATA(rth));
        ipaddr = ntohl(ipaddr);
        Ip4Address addr(ipaddr);
        if (nlh->nlmsg_type == RTM_DELADDR) {
            /*
             * TODO: need to have an un-init of router-id dependencies
             * and reset the router-id at Delete of vhost ip
             */
            DeleteVhostRoutes(addr, ifa->ifa_prefixlen);
        } else {
            if (!agent_->GetRouterIdConfigured()) {
                dep_init_reqd = true; 
            }
            CreateVhostRoutes(addr, ifa->ifa_prefixlen);
            /* Initialize things that are dependent on router-id */
            if (dep_init_reqd) {
                RouterIdDepInit();
                dep_init_reqd = false;
            }
        }
    }

}

int VnswInterfaceListener::NlMsgDecode(struct nlmsghdr *nl, std::size_t len, 
                                       uint32_t seq_no) {
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
            IfaddrHandler(nlh);
            break;
        case RTM_NEWROUTE:
        case RTM_DELROUTE:
            RouteHandler(nlh);
            break;
        case RTM_NEWLINK:
        case RTM_DELLINK:
            InterfaceHandler(nlh);
            break;
        default:
            break;
        }
    }

    return 0;
}

void VnswInterfaceListener::ReadHandler(const boost::system::error_code &error,
                                 std::size_t len) {
    struct nlmsghdr *nlh;

    /* TODO: Handle multiple primary IP address case */
    if (!error) {
        LOG(DEBUG, "Read handler: Received " << len << " bytes");
        nlh = (struct nlmsghdr *)read_buf_;
        NlMsgDecode(nlh, len, -1);
    } else {
        LOG(ERROR, "Error < : " << error.message() << 
                   "> reading packet on netlink sock");
        return;
    }

    if (read_buf_) {
        delete [] read_buf_;
        read_buf_ = NULL;
    }
    RegisterAsyncHandler();
}

void VnswInterfaceListener::Shutdown() { 
    boost::system::error_code ec;
    sock_.close(ec);
}
