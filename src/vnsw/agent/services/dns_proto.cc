/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/logging.h"
#include "oper/interface.h"
#include "oper/mirror_table.h"
#include "dns_proto.h"
#include "services/services_types.h"
#include "services_init.h"
#include "bind/bind_resolver.h"
#include "xmpp/xmpp_channel.h"
#include "cmn/agent_cmn.h"
#include "pugixml/pugixml.hpp"
#include "xml/xml_pugi.h"
#include "bind/xmpp_dns_agent.h"
#include "controller/controller_dns.h"
#include "base/timer.h"

template<> Proto<DnsHandler> *Proto<DnsHandler>::instance_ = NULL;
uint32_t DnsProto::timeout_ = 2000;   // milli seconds
uint32_t DnsProto::max_retries_ = 2;
uint16_t DnsHandler::g_xid_;
DnsHandler::DnsMap DnsHandler::dns_map_;
DnsHandler::DnsStats DnsHandler::stats_;
DnsHandler::DnsRequestMap DnsHandler::req_map_;
DnsHandler::UpdateMap DnsHandler::update_map_;

void DnsProto::Init(boost::asio::io_service &io) {
    assert(Proto<DnsHandler>::instance_ == NULL);
    Proto<DnsHandler>::instance_ = new DnsProto(io);
}

void DnsProto::Shutdown() {
    delete Proto<DnsHandler>::instance_;
    Proto<DnsHandler>::instance_ = NULL;
}

void DnsProto::ConfigInit() {
    std::vector<std::string> dns_servers;
    for (int i = 0; i < MAX_XMPP_SERVERS; i++) {
        std::string server = Agent::GetDnsXmppServer(i);    
        if (server != "")
            dns_servers.push_back(server);
    }
    BindResolver::Init(*Agent::GetEventManager()->io_service(), dns_servers,
                       boost::bind(&DnsHandler::SendDnsIpc, _1));
}

DnsProto::DnsProto(boost::asio::io_service &io) :
    Proto<DnsHandler>("Agent::Services", PktHandler::DNS, io) {
    lid_ = Agent::GetInterfaceTable()->Register(
                  boost::bind(&DnsProto::ItfUpdate, this, _2));
}

DnsProto::~DnsProto() {
    BindResolver::Shutdown();
    Agent::GetInterfaceTable()->Unregister(lid_);
}

void DnsProto::ItfUpdate(DBEntryBase *entry) {
    Interface *itf = static_cast<Interface *>(entry);
    if (itf->GetType() != Interface::VMPORT)
        return;

    VmPortInterface *vmitf = static_cast<VmPortInterface *>(entry);
    if (entry->IsDeleted()) {
        std::string domain;
        DnsHandler::SendDnsUpdateIpc(NULL, DnsAgentXmpp::Update, vmitf, domain);
    } else {
        UpdateDnsEntry(vmitf, vmitf->GetVmName());
    }
}

void DnsProto::UpdateDnsEntry(const VmPortInterface *vmitf,
                              const std::string &name, uint32_t plen,
                              const autogen::IpamType &ipam_type,
                              const autogen::VirtualDnsType &vdns_type) {
    if (!vmitf->GetActiveState())
        return;

    if (!name.size() || BindUtil::HasSpecialChars(name) ||
        !vdns_type.dynamic_records_from_client) {
        DNS_BIND_TRACE(DnsBindTrace, "Not adding DNS entry; Name = " <<
                       name << "Dynamic records allowed = " <<
                       (vdns_type.dynamic_records_from_client ? "yes" : "no"));
        return;
    }

    std::string dns_srv = ipam_type.ipam_dns_server.virtual_dns_server_name;
    BindUtil::RemoveSpecialChars(dns_srv);
    DnsUpdateData *data = new DnsUpdateData();
    data->virtual_dns = dns_srv;
    data->zone = vdns_type.domain_name;

    // Add a DNS record
    uint32_t ip = vmitf->GetIpAddr().to_ulong();
    DnsItem item;
    item.type = DNS_A_RECORD;
    item.ttl = vdns_type.default_ttl_seconds;
    if (name.find(vdns_type.domain_name, 0) == std::string::npos)
        item.name = name + "." + vdns_type.domain_name;
    else
        item.name = name;
    boost::system::error_code ec;
    item.data = vmitf->GetIpAddr().to_string(ec);
    data->items.push_back(item);

    DnsHandler::SendDnsUpdateIpc(data, DnsAgentXmpp::Update, 
                                 vmitf, vdns_type.domain_name);
    DNS_BIND_TRACE(DnsBindTrace, "DNS update sent for : " << item.ToString());

    // Add a PTR record as well
    DnsUpdateData *ptr_data = new DnsUpdateData();
    ptr_data->virtual_dns = dns_srv;
    BindUtil::GetReverseZone(ip, plen, ptr_data->zone);

    item.type = DNS_PTR_RECORD;
    item.data.swap(item.name);
    std::stringstream str;
    for (int i = 0; i < 4; i++) {
        str << ((ip >> (i * 8)) & 0xFF);
        str << ".";
    }
    item.name = str.str() + "in-addr.arpa";
    ptr_data->items.push_back(item);

    DnsHandler::SendDnsUpdateIpc(ptr_data, DnsAgentXmpp::Update, 
                                 vmitf, vdns_type.domain_name);
    DNS_BIND_TRACE(DnsBindTrace, "DNS update sent for : " << item.ToString());
}

void DnsProto::UpdateDnsEntry(const VmPortInterface *vmitf,
                              const std::string &name) {
    if (!vmitf->GetActiveState())
        return;

    autogen::IpamType ipam_type;
    if (!vmitf->GetVnEntry()->GetIpamData(vmitf, ipam_type)) {
        DNS_BIND_TRACE(DnsBindTrace, "Ipam data not found to update DNS entry; VM = "
                       << vmitf->GetName());
        return;
    }

    autogen::VirtualDnsType vdns_type;
    if (ipam_type.ipam_dns_method != "virtual-dns-server" ||
        !DomainConfig::GetVDns(ipam_type.ipam_dns_server.
                               virtual_dns_server_name, vdns_type))
        return;

    Ip4Address ip = vmitf->GetIpAddr();
    uint32_t plen = 0;
    const std::vector<VnIpam> &ipam = vmitf->GetVnEntry()->GetVnIpam();
    unsigned int i;
    for (i = 0; i < ipam.size(); ++i) {
        uint32_t mask = ipam[i].plen ? (0xFFFFFFFF << (32 - ipam[i].plen)) : 0;
        if ((ip.to_ulong() & mask) == (ipam[i].ip_prefix.to_ulong() & mask)) {
            plen = ipam[i].plen;
            break;
        }
    }
    if (i == ipam.size())
        return;

    UpdateDnsEntry(vmitf, name, plen, ipam_type, vdns_type);
}

////////////////////////////////////////////////////////////////////////////////

DnsHandler::DnsHandler(PktInfo *info, boost::asio::io_service &io) : 
    ProtoHandler(info, io), resp_ptr_(NULL), dns_resp_size_(0),
    xid_(-1), retries_(0), action_(NONE), rkey_(NULL), query_name_update_(false),
    pend_req_(0) {
    dns_ = (dnshdr *) pkt_info_->data;
    timer_ = TimerManager::CreateTimer(io, "DnsHandlerTimer");
}

DnsHandler::~DnsHandler() {
    for (ResolvList::iterator it = resolv_list_.begin();
         it != resolv_list_.end(); ++it) {
        delete *it;
    }
    if (rkey_) {
        delete rkey_;
    }
    timer_->Cancel();
    TimerManager::DeleteTimer(timer_);
}

bool DnsHandler::Run() {
    switch(pkt_info_->type) {
        case PktType::MESSAGE:
            return HandleMessage();

       default:
            return HandleRequest();
    }
}    

bool DnsHandler::HandleRequest() {
    stats_.IncrStatsReq();

    uint16_t ret = DNS_ERR_NO_ERROR;
    const Interface *itf = InterfaceTable::FindInterface(GetIntf());
    if (!itf || (itf->GetType() != Interface::VMPORT) || dns_->flags.req) {
        stats_.IncrStatsDrop();
        DNS_BIND_TRACE(DnsBindError, "Received Invalid DNS request - dropping"
                       << "; itf = " << itf << "; flags.req = " 
                       << dns_->flags.req << "; src addr = " 
                       << pkt_info_->ip->saddr <<";");
        return true;
    }

    const VmPortInterface *vmitf = static_cast<const VmPortInterface *>(itf);
    // Handle requests (req == 0), queries (op code == 0), updates, non auth
    if ((dns_->flags.op && dns_->flags.op != DNS_OPCODE_UPDATE) || 
        (dns_->flags.cd)) {
        DNS_BIND_TRACE(DnsBindError, "Unimplemented DNS request"
                       << "; flags.op = " << dns_->flags.op << "; flags.cd = "
                       << dns_->flags.cd << ";");
        ret = DNS_ERR_NO_IMPLEMENT;
        goto error;
    }

    if (!vmitf->GetVnEntry() || 
        !vmitf->GetVnEntry()->GetIpamData(vmitf, ipam_type_)) {
        DNS_BIND_TRACE(DnsBindError, "Unable to find Ipam data; interface = "
                       << vmitf->GetName());
        ret = DNS_ERR_SERVER_FAIL;
        goto error;
    }

    if (ipam_type_.ipam_dns_method == "default-dns-server" ||
        ipam_type_.ipam_dns_method == "") {
        if (dns_->flags.op == DNS_OPCODE_UPDATE) {
            DNS_BIND_TRACE(DnsBindError, "Default DNS request : Update received, ignoring");
            ret = DNS_ERR_NO_IMPLEMENT;
            goto error;
        }
        return HandleDefaultDnsRequest(vmitf);
    } else if (ipam_type_.ipam_dns_method == "virtual-dns-server") {
        if (!DomainConfig::GetVDns(ipam_type_.ipam_dns_server.
                                   virtual_dns_server_name, vdns_type_)) {
            DNS_BIND_TRACE(DnsBindError, "Unable to find domain; interface = "
                           << vmitf->GetName());
            ret = DNS_ERR_SERVER_FAIL;
            goto error;
        }
        return HandleVirtualDnsRequest(vmitf);
    }

    ret = DNS_ERR_SERVER_FAIL;
error:
    BindUtil::BuildDnsHeader(dns_, ntohs(dns_->xid), DNS_QUERY_RESPONSE,
                             DNS_OPCODE_QUERY, 0, 1, ret,
                             ntohs(dns_->ques_rrcount));
    SendDnsResponse();
    return true;
}

bool DnsHandler::HandleDefaultDnsRequest(const VmPortInterface *vmitf) {
    tbb::mutex::scoped_lock lock(mutex_);
    rkey_ = new QueryKey(vmitf, dns_->xid);
    if (req_map_.find(*rkey_) != req_map_.end()) {
        DNS_BIND_TRACE(DnsBindTrace, 
                       "Retry DNS query from VM - dropping; interface = "
                       << vmitf->GetName() << " xid = " << dns_->xid);
        stats_.IncrStatsRetransmitReq();
        return true;
    }
    req_map_.insert(DnsRequestPair(*rkey_, this));

    dns_resp_size_ = BindUtil::ParseDnsQuery((uint8_t *)dns_, items_);
    resp_ptr_ = (uint8_t *)dns_ + dns_resp_size_;
    action_ = DnsHandler::DNS_QUERY;
    BindUtil::BuildDnsHeader(dns_, ntohs(dns_->xid), DNS_QUERY_RESPONSE, 
                             DNS_OPCODE_QUERY, 0, 1, DNS_ERR_NO_ERROR, 
                             ntohs(dns_->ques_rrcount));
    for (uint32_t i = 0; i < items_.size(); i++) {
        ResolveHandler resolv_handler = 
            boost::bind(&DnsHandler::DefaultDnsResolveHandler, this, _1, _2, i);

        boost_udp::resolver *resolv = new boost_udp::resolver(io_);
        switch(items_[i].type) {
            case DNS_A_RECORD:
                resolv->async_resolve(
                        boost_udp::resolver::query(boost_udp::v4(), 
                        items_[i].name, "domain"), resolv_handler);
                break;

            case DNS_PTR_RECORD: {
                uint32_t addr;
                if (!BindUtil::GetAddrFromPtrName(items_[i].name, addr)) {
                    DNS_BIND_TRACE(DnsBindError, "Default DNS request:"
                                   " interface = " << vmitf->GetName() <<
                                   " Ignoring invalid IP in PTR type : " + 
                                   items_[i].name);
                    delete resolv;
                    continue;
                }
                boost_udp::endpoint ep;
                ep.address(boost::asio::ip::address_v4(addr));
                resolv->async_resolve(ep, resolv_handler);
                break;
            }

            case DNS_AAAA_RECORD:
                resolv->async_resolve(
                        boost_udp::resolver::query(boost_udp::v6(),
                        items_[i].name, "domain"), resolv_handler);
                break;

            default:
                DNS_BIND_TRACE(DnsBindError, "Default DNS request:"
                               " interface = " << vmitf->GetName() <<
                               " Ignoring unsupported type : " << 
                               items_[i].type);
                break;
        }
        resolv_list_.push_back(resolv);
        pend_req_++;
    }

    DNS_BIND_TRACE(DnsBindTrace, "Default DNS query sent to server; "
                   "interface = " << vmitf->GetName() << " xid = " << 
                   dns_->xid << "; " << DnsItemsToString(items_) << ";");
    if (pend_req_)
        return false;

    req_map_.erase(*rkey_);
    return true;
}

void DnsHandler::DefaultDnsResolveHandler(const error_code& error,
                                          boost_udp::resolver::iterator it,
                                          uint32_t index) {
    {
        tbb::mutex::scoped_lock lock(mutex_);
        if (error) {
            dns_->flags.ret = DNS_ERR_NO_SUCH_NAME;
        } else {
            bool resolved = true;
            items_[index].ttl = DEFAULT_DNS_TTL;
            if (items_[index].type == DNS_A_RECORD) {
                boost_udp::resolver::iterator end;
                while (it != end) {
                    boost_udp::endpoint ep = *it;
                    if (ep.address().is_v4() && 
                        !ep.address().to_v4().is_unspecified())
                        items_[index].data = ep.address().to_v4().to_string();
                    else {     
                        dns_->flags.ret = DNS_ERR_SERVER_FAIL;
                        resolved = false;
                    }
                    it++;
                }
            } else if (items_[index].type == DNS_PTR_RECORD) {
                items_[index].data = it->host_name();
            } else if (items_[index].type == DNS_AAAA_RECORD) {
                boost_udp::resolver::iterator end;
                while (it != end) {
                    boost_udp::endpoint ep = *it;
                    if (ep.address().is_v6() && 
                        !ep.address().to_v6().is_unspecified())
                        items_[index].data = ep.address().to_v6().to_string();
                    else {     
                        dns_->flags.ret = DNS_ERR_SERVER_FAIL;
                        resolved = false;
                    }
                    it++;
                }
            }

            if (resolved) {
                resp_ptr_ = BindUtil::AddAnswerSection(resp_ptr_, items_[index], 
                                                       dns_resp_size_);
                dns_->ans_rrcount++;
            }
        }

        pend_req_--;
        if (pend_req_)
            return;

        dns_->ans_rrcount = htons(dns_->ans_rrcount);
    }
    SendDnsIpc(DnsHandler::DNS_DEFAULT_RESPONSE, 0, NULL, this);
}

void DnsHandler::DefaultDnsSendResponse() {
    req_map_.erase(*rkey_);
    if (dns_->flags.ret) {
        DNS_BIND_TRACE(DnsBindError, "Query failed : " << 
                       BindUtil::DnsResponseCode(dns_->flags.ret) <<
                       "; xid = " << dns_->xid << "; " <<
                       DnsItemsToString(items_) << ";");
    } else {
        DNS_BIND_TRACE(DnsBindTrace, "Query successful : xid = " <<
                       dns_->xid << ";" << DnsItemsToString(items_) << ";");
    }
    SendDnsResponse();
}

bool DnsHandler::HandleVirtualDnsRequest(const VmPortInterface *vmitf) {
    rkey_ = new QueryKey(vmitf, dns_->xid);
    if (req_map_.find(*rkey_) != req_map_.end()) {
        DNS_BIND_TRACE(DnsBindTrace, 
                       "Retry DNS query from VM - dropping; interface = " <<
                       vmitf->GetName() << " xid = " << dns_->xid << "; " << 
                       DnsItemsToString(items_) << ";");
        stats_.IncrStatsRetransmitReq();
        return true;
    }
    req_map_.insert(DnsRequestPair(*rkey_, this));

    BindUtil::RemoveSpecialChars(ipam_type_.ipam_dns_server.
                                 virtual_dns_server_name);

    uint16_t ret = DNS_ERR_NO_ERROR;
    switch (dns_->flags.op) {
        case DNS_OPCODE_QUERY: {
            dns_resp_size_ = BindUtil::ParseDnsQuery((uint8_t *)dns_, items_);
            resp_ptr_ = (uint8_t *)dns_ + dns_resp_size_;
            UpdateQueryNames();
            xid_ = GetTransId();
            action_ = DnsHandler::DNS_QUERY;
            BindUtil::BuildDnsHeader(dns_, ntohs(dns_->xid), DNS_QUERY_RESPONSE, 
                                     DNS_OPCODE_QUERY, 0, 1, ret, 
                                     ntohs(dns_->ques_rrcount));
            if (SendDnsQuery())
                return false;
            break;
        }

        case DNS_OPCODE_UPDATE: {
            if (vdns_type_.dynamic_records_from_client) {
                DnsUpdateData *update_data = new DnsUpdateData();
                DnsUpdateIpc *update = new DnsUpdateIpc(DnsAgentXmpp::Update, 
                                           update_data, vmitf, 
                                           vdns_type_.domain_name);
                if (BindUtil::ParseDnsUpdate((uint8_t *)dns_, *update_data)) {
                    update_data->virtual_dns = ipam_type_.ipam_dns_server.
                                               virtual_dns_server_name;
                    UpdateNames(update_data, vdns_type_.domain_name);
                    Update(update);
                } else {
                    delete update;
                    ret = DNS_ERR_NO_IMPLEMENT;
                }
            } else {
                DNS_BIND_TRACE(DnsBindTrace, "Client not allowed to update "
                "dynamic records : " << vmitf->GetName() << " ;Ignoring "
                "update for " << DnsItemsToString(items_) << ";");
                ret = DNS_ERR_NOT_AUTH;
            }
            BindUtil::BuildDnsHeader(dns_, ntohs(dns_->xid), DNS_QUERY_RESPONSE, 
                                     DNS_OPCODE_UPDATE, 0, 1, ret,
                                     ntohs(dns_->ques_rrcount));
            SendDnsResponse();
            break;
        }

        default: {
            DNS_BIND_TRACE(DnsBindTrace, "Unsupported op : " << dns_->flags.op
                           << "from vm : " << vmitf->GetName());
            break;
        }
    }
    req_map_.erase(*rkey_);
    return true;
}

bool DnsHandler::SendDnsQuery() {
    uint8_t *pkt = NULL;
    std::size_t len = 0;
    DnsMap::iterator it = dns_map_.find(xid_);
    if (it == dns_map_.end()) {
        dns_map_.insert(DnsPair(xid_, this));
    } else {
        if (retries_ >= DnsProto::GetMaxRetries()) {
            DNS_BIND_TRACE(DnsBindTrace, 
                           "Max retries reached for query; xid = " << xid_ <<
                           "; " << DnsItemsToString(items_) << ";");
            goto cleanup;
        } else
            retries_++;
    }

    pkt = new uint8_t[BindResolver::max_pkt_size];
    len = BindUtil::BuildDnsQuery(pkt, xid_, 
          ipam_type_.ipam_dns_server.virtual_dns_server_name, items_);
    if (BindResolver::Resolver()->DnsSend(pkt, Agent::GetXmppDnsCfgServerIdx(),
                                          len)) {
        DNS_BIND_TRACE(DnsBindTrace, "DNS query sent to server; xid = " << 
                       xid_ << "; " << DnsItemsToString(items_) << ";");
        timer_->Start(DnsProto::GetTimeout(),
                      boost::bind(&DnsHandler::TimerExpiry, this, xid_));
        return true;
    } 

cleanup:
    stats_.IncrStatsDrop();
    if (it != dns_map_.end()) {
        dns_map_.erase(it);
    }
    return false;
}

bool DnsHandler::HandleMessage() {
    switch (pkt_info_->ipc->cmd) {
        case DnsHandler::DNS_DEFAULT_RESPONSE:
            return HandleDefaultDnsResponse();

        case DnsHandler::DNS_BIND_RESPONSE:
            return HandleBindResponse();

        case DnsHandler::DNS_TIMER_EXPIRED:
            return HandleRetryExpiry();

        case DnsHandler::DNS_XMPP_SEND_UPDATE:
            return HandleUpdate();

        case DnsHandler::DNS_XMPP_UPDATE_RESPONSE:
            return HandleUpdateResponse();

        case DnsHandler::DNS_XMPP_SEND_UPDATE_ALL:
            return UpdateAll();

        default:
            assert(0);
    }
}

bool DnsHandler::HandleDefaultDnsResponse() {
    DnsIpc *ipc = static_cast<DnsIpc *>(pkt_info_->ipc);
    ipc->handler->DefaultDnsSendResponse();
    delete ipc;
    return true;
}

bool DnsHandler::HandleBindResponse() {
    DnsIpc *ipc = static_cast<DnsIpc *>(pkt_info_->ipc);
    uint16_t xid = ntohs(*(uint16_t *)ipc->resp);
    DnsMap::iterator it = dns_map_.find(xid);
    if (it != dns_map_.end()) {
        DnsHandler *handler = it->second;
        dns_flags flags;
        std::vector<DnsItem> ques, ans, auth, add;
        BindUtil::ParseDnsQuery(ipc->resp, xid, flags, ques, ans, auth, add);
        switch(handler->action_) {
            case DnsHandler::DNS_QUERY:
                handler->Resolve(flags, ques, ans, auth, add);
                if (flags.ret) {
                    DNS_BIND_TRACE(DnsBindError, "Query failed : " << 
                                   BindUtil::DnsResponseCode(flags.ret) <<
                                   "; xid = " << xid << "; " <<
                                   DnsItemsToString(items_) << ";");
                } else {
                    DNS_BIND_TRACE(DnsBindTrace, "Query successful : xid = " <<
                                   xid << ";" << DnsItemsToString(ans) << ";");
                }
                break;

            default:
                DNS_BIND_TRACE(DnsBindTrace, 
                               "Received invalid BIND response: xid = " << xid);
                break;
        }
        dns_map_.erase(it);
        req_map_.erase(*handler->rkey_);
        delete handler;
    } else
        DNS_BIND_TRACE(DnsBindError, "Invalid xid " << xid << 
                       "received from DNS server - dropping");

    delete ipc;
    return true;
}

bool DnsHandler::HandleRetryExpiry() {
    DnsIpc *ipc = static_cast<DnsIpc *>(pkt_info_->ipc);
    DnsMap::iterator it = dns_map_.find(ipc->xid);
    if (it != dns_map_.end()) {
        DnsHandler *handler = it->second;
        if (!handler->SendDnsQuery()) {
            req_map_.erase(*handler->rkey_);
            delete handler;
        }
    }

    delete ipc;
    return true;
}

bool DnsHandler::HandleUpdateResponse() {
    DnsUpdateIpc *ipc = static_cast<DnsUpdateIpc *>(pkt_info_->ipc);
    delete ipc;
    return true;
}

bool DnsHandler::HandleUpdate() {
    DnsUpdateIpc *ipc = static_cast<DnsUpdateIpc *>(pkt_info_->ipc);
    if (!ipc->xmpp_data) {
        DelUpdate(ipc);
    } else {
        UpdateNames(ipc->xmpp_data, ipc->domain);
        Update(ipc);
    }
    return true;
}

bool DnsHandler::UpdateAll() {
    DnsUpdateAllIpc *ipc = static_cast<DnsUpdateAllIpc *>(pkt_info_->ipc);
    for (UpdateMap::iterator it = update_map_.begin(); 
         it != update_map_.end(); ++ it) {
        SendXmppUpdate(ipc->channel, (*it)->xmpp_data);
    }

    delete ipc;
    return true;
}

void DnsHandler::SendXmppUpdate(AgentDnsXmppChannel *channel, 
                                DnsUpdateData *xmpp_data) {
    if (channel) {
        // Split the request in case we have more data items
        DnsItems done, store;
        DnsItems::iterator first, last;

        uint32_t size = xmpp_data->items.size();
        store.swap(xmpp_data->items);
        for (uint32_t i = 0; i <= size / max_items_per_xmpp_msg; i++) {
            if (!store.size())
                break;

            first = last = store.begin();
            if (store.size() > max_items_per_xmpp_msg)
                std::advance(last, max_items_per_xmpp_msg);
            else
                last = store.end();
            xmpp_data->items.splice(xmpp_data->items.begin(), store, first, last);

            uint8_t *data = new uint8_t[DnsAgentXmpp::max_dns_xmpp_msg_len];
            xid_ = GetTransId();
            std::size_t len = 0;
            if ((len = DnsAgentXmpp::DnsAgentXmppEncode(channel->GetXmppChannel(), 
                                                        DnsAgentXmpp::Update,
                                                        xid_, 0, xmpp_data, 
                                                        data)) > 0) {
                channel->SendMsg(data, len);
            }
            else 
                delete data;

            done.splice(done.end(), xmpp_data->items, xmpp_data->items.begin(),
                        xmpp_data->items.end());
        }
        xmpp_data->items.swap(done);
    }
}

void 
DnsHandler::Resolve(dns_flags flags, std::vector<DnsItem> &ques, 
                    std::vector<DnsItem> &ans, std::vector<DnsItem> &auth, 
                    std::vector<DnsItem> &add) {
    for(unsigned int i = 0; i < ans.size(); ++i) {
        // find the matching entry in the request
        bool name_update_required = true;
        for (unsigned int j = 0; j < items_.size(); ++j) {
            if (ans[i].name == items_[j].name &&
                ans[i].eclass == items_[j].eclass) {
                ans[i].name_plen = items_[j].name_plen;
                ans[i].name_offset = items_[j].offset;
                name_update_required = false;
                break;
            }
        }
        UpdateOffsets(ans[i], name_update_required);
        UpdateGWAddress(ans[i]);
        resp_ptr_ = BindUtil::AddAnswerSection(resp_ptr_, ans[i], 
                                               dns_resp_size_);
        dns_->ans_rrcount++;
    }

    for(unsigned int i = 0; i < auth.size(); ++i) {
        UpdateOffsets(auth[i], true);
        UpdateGWAddress(auth[i]);
        resp_ptr_ = BindUtil::AddAnswerSection(resp_ptr_, auth[i], 
                                               dns_resp_size_);
        dns_->auth_rrcount++;
    }

    for(unsigned int i = 0; i < add.size(); ++i) {
        UpdateOffsets(add[i], true);
        UpdateGWAddress(add[i]);
        resp_ptr_ = BindUtil::AddAnswerSection(resp_ptr_, add[i], 
                                               dns_resp_size_);
        dns_->add_rrcount++;
    }

    dns_->flags.ret = flags.ret;
    dns_->flags.auth = flags.auth;
    dns_->flags.ad = flags.ad;
    dns_->flags.ra = flags.ra;
    dns_->ans_rrcount = htons(dns_->ans_rrcount);
    dns_->auth_rrcount = htons(dns_->auth_rrcount);
    dns_->add_rrcount = htons(dns_->add_rrcount);
    SendDnsResponse();
}

void DnsHandler::SendDnsResponse() {
    in_addr_t src_ip = pkt_info_->ip->daddr;
    in_addr_t dest_ip = pkt_info_->ip->saddr;
    unsigned char dest_mac[MAC_ALEN];
    memcpy(dest_mac, pkt_info_->eth->h_source, MAC_ALEN);

    // fill in the response
    dns_resp_size_ += sizeof(udphdr);
    UdpHdr(dns_resp_size_, src_ip, DNS_SERVER_PORT, 
           dest_ip, ntohs(pkt_info_->transp.udp->source));
    dns_resp_size_ += sizeof(iphdr);
    IpHdr(dns_resp_size_, src_ip, dest_ip, UDP_PROTOCOL);
    EthHdr(agent_vrrp_mac, dest_mac, 0x800);
    dns_resp_size_ += sizeof(ethhdr);

    HostInterfaceKey key(nil_uuid(), Agent::GetHostIfname());
    Interface *pkt_itf = static_cast<Interface *>
                         (Agent::GetInterfaceTable()->FindActiveEntry(&key));
    if (pkt_itf) {
        UpdateStats();
        Send(dns_resp_size_, GetIntf(), pkt_info_->vrf,
             AGENT_CMD_ROUTE, PktHandler::DNS);
    } else
        stats_.IncrStatsDrop();
}

inline uint16_t DnsHandler::GetTransId() {
    return (++g_xid_ == 0 ? ++g_xid_ : g_xid_);
}

void DnsHandler::UpdateQueryNames() {
    for (unsigned int i = 0; i < items_.size(); ++i) {
        if (items_[i].name.find('.', 0) == std::string::npos) {
            items_[i].name.append(".");
            items_[i].name.append(vdns_type_.domain_name);
            query_name_update_ = true;
        }
    }
}

// In case we added domain name to the queries, the response to the VM 
// should not have the domain name. Update the offsets in the DnsItems
// accordingly.
void DnsHandler::UpdateOffsets(DnsItem &item, bool name_update_required) {
    if (!query_name_update_)
        return;

    uint16_t msg_offset = (resp_ptr_ - (uint8_t *)dns_) | 0xC000;
    if (name_update_required) {
        name_encoder_.AddName(item.name, msg_offset, item.name_plen,
                              item.name_offset);
    }
    msg_offset += BindUtil::DataLength(item.name_plen, item.name_offset,
                                       item.name.size()) + 10;
    if (item.type == DNS_TYPE_SOA) {
        name_encoder_.AddName(item.soa.primary_ns, msg_offset, item.soa.ns_plen,
                              item.soa.ns_offset);
        msg_offset += BindUtil::DataLength(item.soa.ns_plen, item.soa.ns_offset,
                                           item.soa.primary_ns.size());
        name_encoder_.AddName(item.soa.mailbox, msg_offset,
                              item.soa.mailbox_plen, item.soa.mailbox_offset);
    } else {
        name_encoder_.AddName(item.data, msg_offset,
                              item.data_plen, item.data_offset);
    }
}

void DnsHandler::UpdateGWAddress(DnsItem &item) {
    boost::system::error_code ec;
    if (item.type == DNS_A_RECORD && item.data == "127.0.0.1") {
        boost::asio::ip::address_v4 addr(pkt_info_->ip_daddr);
        item.data = addr.to_string(ec);
    }
}

void DnsHandler::UpdateNames(DnsUpdateData *data, std::string &domain) {
    for (DnsItems::iterator item = data->items.begin();
         item != data->items.end(); ++item) {
        if (item->type == DNS_A_RECORD || item->type == DNS_AAAA_RECORD) {
            item->name = BindUtil::GetFQDN(item->name, data->zone, data->zone);
        } else if (item->type == DNS_PTR_RECORD) {
            item->data = BindUtil::GetFQDN(item->data, domain, domain);
        } else if (item->type == DNS_CNAME_RECORD) {
            item->name = BindUtil::GetFQDN(item->name, data->zone, data->zone);
            item->data = BindUtil::GetFQDN(item->data, data->zone, ".");
        }
    }
}

void DnsHandler::Update(DnsUpdateIpc *update) {
    bool free_update = true;
    UpdateMap::iterator it = update_map_.find(update);
    if (it == update_map_.end()) {
        update_map_.insert(update);
        free_update = false;
    } else {
        DnsUpdateData *data = (*it)->xmpp_data;
        for (DnsItems::iterator item = update->xmpp_data->items.begin(); 
             item != update->xmpp_data->items.end();) {
            if ((*item).IsDelete()) {
                if (!data->DelItem(*item))
                    update->xmpp_data->items.erase(item++);
                else
                    ++item;
            } else {
                if (!data->AddItem(*item))
                    update->xmpp_data->items.erase(item++);
                else
                    ++item;
            }
        }
        if (!data->items.size()) {
            update_map_.erase(it);
            delete (*it);
        }
        if (!update->xmpp_data->items.size())
            goto done;
    }

    for (int i = 0; i < MAX_XMPP_SERVERS; i++) {
        AgentDnsXmppChannel *channel = Agent::GetAgentDnsXmppChannel(i);
        SendXmppUpdate(channel, update->xmpp_data);
    }

done:
    if (free_update)
        delete update;
}

void DnsHandler::DelUpdate(DnsUpdateIpc *update) {
    UpdateMap::iterator it = update_map_.find(update);
    while (it != update_map_.end()) {
        for (DnsItems::iterator item = (*it)->xmpp_data->items.begin(); 
             item != (*it)->xmpp_data->items.end(); ++item) {
            // in case of delete, set the class to NONE and ttl to 0
            (*item).eclass = DNS_CLASS_NONE;
            (*item).ttl = 0;
        }
        for (int i = 0; i < MAX_XMPP_SERVERS; i++) {
            AgentDnsXmppChannel *channel = Agent::GetAgentDnsXmppChannel(i);
            SendXmppUpdate(channel, (*it)->xmpp_data);
        }
        delete (*it);
        update_map_.erase(it);

        it = update_map_.find(update);
    }
    delete update;
}

void DnsHandler::UpdateStats() {
    switch (dns_->flags.ret) {
        case DNS_ERR_NO_ERROR:
            stats_.IncrStatsRes();
            break;

        case DNS_ERR_NO_IMPLEMENT:
            stats_.IncrStatsUnsupp();
            break;

        case DNS_ERR_FORMAT_ERROR:
        case DNS_ERR_NO_SUCH_NAME:
        case DNS_ERR_SERVER_FAIL:
        case DNS_ERR_NOT_AUTH:
        default:
            stats_.IncrStatsFail();
            break;
    }
}

bool DnsHandler::TimerExpiry(uint16_t xid) {
    SendDnsIpc(DnsHandler::DNS_TIMER_EXPIRED, xid);
    return false;
}

std::string DnsHandler::DnsItemsToString(std::vector<DnsItem> &items) {
    std::string str;
    for (unsigned int i = 0; i < items.size(); ++i) {
        str.append(items[i].ToString());
        str.append(" ");
    }
    return str;
}

void DnsHandler::SendDnsIpc(uint8_t *pkt) {
    DnsIpc *ipc = new DnsIpc(pkt, 0, NULL, DnsHandler::DNS_BIND_RESPONSE);
    PktHandler::GetPktHandler()->SendMessage(PktHandler::DNS, ipc);
}

void DnsHandler::SendDnsIpc(IpcCommand cmd, uint16_t xid, uint8_t *msg,
                            DnsHandler *handler) {
    DnsIpc *ipc = new DnsIpc(msg, xid, handler, cmd);
    PktHandler::GetPktHandler()->SendMessage(PktHandler::DNS, ipc);
}

void DnsHandler::SendDnsUpdateIpc(DnsUpdateData *data,
                                  DnsAgentXmpp::XmppType type,
                                  const VmPortInterface *vm,
                                  const std::string &domain) {
    DnsUpdateIpc *ipc = new DnsUpdateIpc(type, data, vm, domain);
    PktHandler::GetPktHandler()->SendMessage(PktHandler::DNS, ipc);
}

void DnsHandler::SendDnsUpdateIpc(AgentDnsXmppChannel *channel) {
    DnsUpdateAllIpc *ipc = new DnsUpdateAllIpc(channel);
    PktHandler::GetPktHandler()->SendMessage(PktHandler::DNS, ipc);
}

