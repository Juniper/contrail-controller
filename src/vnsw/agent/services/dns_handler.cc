/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <stdint.h>
#include "base/os.h"
#include "vr_defs.h"
#include "oper/interface_common.h"
#include "services/dns_proto.h"
#include "services/dhcp_proto.h"
#include "bind/bind_resolver.h"
#include "cmn/agent_cmn.h"
#include "controller/controller_dns.h"
#include "base/timer.h"
#include "oper/operdb_init.h"
#include "oper/global_vrouter.h"
#include "oper/vn.h"

DnsHandler::DnsHandler(Agent *agent, boost::shared_ptr<PktInfo> info,
                       boost::asio::io_service &io)
    : ProtoHandler(agent, info, io), resp_ptr_(NULL), dns_resp_size_(0),
      xid_(-1), action_(NONE), rkey_(NULL),
      query_name_update_(false), pend_req_(0), default_method_(false),
      curr_index_(0) {
    dns_ = (dnshdr *) pkt_info_->data;
}

DnsHandler::~DnsHandler() {
    for (ResolvList::iterator it = resolv_list_.begin();
         it != resolv_list_.end(); ++it) {
        delete *it;
    }
    if (rkey_) {
        delete rkey_;
    }

    uint8_t count = 0;
    while (count < dns_resolvers_.size()) {
        if (dns_resolvers_[count]) {
            dns_resolvers_[count]->timer_->Cancel();
            TimerManager::DeleteTimer(dns_resolvers_[count]->timer_);
            delete dns_resolvers_[count];
        }
        count++;
    }
    count = 0;
    while (count < def_dns_resolvers_.size()) {
        if (def_dns_resolvers_[count]) {
            def_dns_resolvers_[count]->timer_->Cancel();
            TimerManager::DeleteTimer(def_dns_resolvers_[count]->timer_);
            delete def_dns_resolvers_[count];
        }
        count++;
    }

    dns_resolvers_.clear();
    def_dns_resolvers_.clear();
}


void DnsHandler::BuildDnsResolvers() {

    uint8_t count = 0;
    uint8_t resolvers_count = Agent::GetInstance()->GetDnslist().size();
    dns_resolvers_.resize(resolvers_count);
    
    std::vector<string>dns_servers;
    while (count < resolvers_count) {
   
        boost::split(dns_servers, Agent::GetInstance()->GetDnslist()[count], 
                     boost::is_any_of(":"));
        DnsResolverInfo *resolver = new DnsResolverInfo();

        boost::system::error_code ec;
        resolver->ep_.address(boost::asio::ip::address::from_string(
                              dns_servers[0], ec));
        assert(ec.value() == 0);
        uint16_t dns_port = strtoul(dns_servers[1].c_str(), NULL, 10);
        resolver->ep_.port(dns_port);
        resolver->retries_ = 0;
        std::stringstream ss;
        ss << "DnsHandlerTimer " << count;
        resolver->timer_ = TimerManager::CreateTimer(
                *(agent()->event_manager()->io_service()), ss.str(),
                TaskScheduler::GetInstance()->GetTaskId("Agent::Services"),
                PktHandler::DNS);
        dns_resolvers_[count] = resolver;

        count++;
    }
}

void DnsHandler::BuildDefaultDnsResolvers() {
    uint8_t count = 0;
    DnsProto *dns_proto = agent()->GetDnsProto();
    std::vector<IpAddress> const &def_slist = dns_proto->GetDefaultServerList();
    def_dns_resolvers_.resize(def_slist.size());
    while (count < def_slist.size()) {
        DnsResolverInfo *resolver = new DnsResolverInfo();

        resolver->ep_.address(def_slist[count]);
        resolver->ep_.port(DNS_SERVER_PORT);
        resolver->retries_ = 0;
        std::stringstream ss;
        ss << "DefDnsHandlerTimer " << count;
        resolver->timer_ = TimerManager::CreateTimer(
            *(agent()->event_manager()->io_service()), ss.str(),
            TaskScheduler::GetInstance()->GetTaskId("Agent::Services"),
            PktHandler::DNS);
        def_dns_resolvers_[count] = resolver;
        count++;
    }
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
    DnsProto *dns_proto = agent()->GetDnsProto();
    dns_proto->IncrStatsReq();

    uint16_t ret = DNS_ERR_NO_ERROR;
    const Interface *itf =
        agent()->interface_table()->FindInterface(GetInterfaceIndex());
    if (!itf || (itf->type() != Interface::VM_INTERFACE) ||
        dns_->flags.req) {
        dns_proto->IncrStatsDrop();
        DNS_BIND_TRACE(DnsBindError, "Received Invalid DNS request - dropping"
                       << "; itf = " << itf << "; flags.req = "
                       << dns_->flags.req << "; src addr = "
                       << pkt_info_->ip->ip_src.s_addr <<";");
        return true;
    }

    const VmInterface *vmitf = static_cast<const VmInterface *>(itf);
    if (!vmitf->layer3_forwarding()) {
        DNS_BIND_TRACE(DnsBindError, "DNS request on VM port with disabled" 
                       "ipv4 service: " << itf);
        dns_proto->IncrStatsDrop();
        return true;
    }
    // Handle requests (req == 0), queries (op code == 0), updates, non auth
    if ((dns_->flags.op && dns_->flags.op != DNS_OPCODE_UPDATE) || 
        (dns_->flags.cd)) {
        DNS_BIND_TRACE(DnsBindError, "Unimplemented DNS request"
                       << "; flags.op = " << dns_->flags.op << "; flags.cd = "
                       << dns_->flags.cd << ";");
        ret = DNS_ERR_NO_IMPLEMENT;
        goto error;
    }

    if (!vmitf->vn() ||
        (pkt_info_->ip_saddr.is_v4() &&
         !vmitf->vn()->GetIpamData(vmitf->primary_ip_addr(),
                                   &ipam_name_, &ipam_type_)) ||
        (pkt_info_->ip_saddr.is_v6() &&
         !vmitf->vn()->GetIpamData(vmitf->primary_ip6_addr(),
                                   &ipam_name_, &ipam_type_))) {
        DNS_BIND_TRACE(DnsBindError, "Unable to find Ipam data; interface = "
                       << vmitf->name());
        ret = DNS_ERR_SERVER_FAIL;
        goto error;
    }

    if (ipam_type_.ipam_dns_method == "default-dns-server" ||
        ipam_type_.ipam_dns_method == "") {
        BuildDefaultDnsResolvers();
        if (dns_->flags.op == DNS_OPCODE_UPDATE) {
            DNS_BIND_TRACE(DnsBindError, "Default DNS request : Update received, ignoring");
            ret = DNS_ERR_NO_IMPLEMENT;
            goto error;
        }
        GetDomainName(vmitf, &domain_name_);
        default_method_ = true;
        return HandleDefaultDnsRequest(vmitf);
    } else if (ipam_type_.ipam_dns_method == "virtual-dns-server") {
        BuildDnsResolvers();
        if (!agent()->domain_config_table()->GetVDns(ipam_type_.ipam_dns_server.
            virtual_dns_server_name, &vdns_type_)) {
            DNS_BIND_TRACE(DnsBindError, "Unable to find domain; interface = "
                           << vmitf->vm_name());
            ret = DNS_ERR_SERVER_FAIL;
            goto error;
        }
        domain_name_ = vdns_type_.domain_name;
        default_method_ = false;
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

bool DnsHandler::HandleDefaultDnsRequest(const VmInterface *vmitf) {
    tbb::mutex::scoped_lock lock(mutex_);
    uint16_t ret = DNS_ERR_NO_ERROR;
    DnsProto *dns_proto = agent()->GetDnsProto();
    rkey_ = new QueryKey(vmitf, dns_->xid);
    if (dns_proto->IsVmRequestDuplicate(rkey_)) {
        DNS_BIND_TRACE(DnsBindTrace, 
                       "Retry DNS query from VM - dropping; interface = "
                       << vmitf->vm_name() << " xid = " << dns_->xid);
        dns_proto->IncrStatsRetransmitReq();
        return true;
    }
    dns_proto->AddVmRequest(rkey_);

    if (BindUtil::ParseDnsQuery((uint8_t *)dns_,
                                pkt_info_->GetUdpPayloadLength(),
                                &dns_resp_size_, items_) == false) {
        DNS_BIND_TRACE(DnsBindTrace, "Default DNS query parsing failed; "
                       "interface = " << vmitf->vm_name() << " xid = " <<
                       dns_->xid);
        return true;
    }

    resp_ptr_ = (uint8_t *)dns_ + dns_resp_size_;
    BindUtil::BuildDnsHeader(dns_, ntohs(dns_->xid), DNS_QUERY_RESPONSE, 
                             DNS_OPCODE_QUERY, 0, 1, DNS_ERR_NO_ERROR, 
                             ntohs(dns_->ques_rrcount));
    ResolveAllLinkLocalRequests();
    if (!items_.size()) {
        // no pending resolution, send response
        dns_->ans_rrcount = htons(dns_->ans_rrcount);
        DefaultDnsSendResponse();
        dns_proto->DelVmRequest(rkey_);
        return true;
    }

    if (!def_dns_resolvers_.size()) {
        DNS_BIND_TRACE(DnsBindTrace, "No DNS resolvers for Default DNS query"
                       " with xid = " << dns_->xid << ";interface = "
                       << vmitf->vm_name());
        ret = DNS_ERR_SERVER_FAIL;
        goto fail;
    }

    DNS_BIND_TRACE(DnsBindTrace, "Default DNS query received; "
                   "interface = " << vmitf->vm_name() << " xid = " << 
                   dns_->xid << " " << DnsItemsToString(items_));

    action_ = DnsHandler::DNS_QUERY;
    curr_index_ = 0;
    if (SendToDefaultServer()) {
        return false;
    }

    DNS_BIND_TRACE(DnsBindTrace, "Unable to send DNS query to resolvers;"
                   " xid = " << dns_->xid << "; interface = "
                   << vmitf->vm_name());
    ret = DNS_ERR_SERVER_FAIL;
fail:
    BindUtil::BuildDnsHeader(dns_, ntohs(dns_->xid), DNS_QUERY_RESPONSE,
                             DNS_OPCODE_QUERY, 0, 1, ret,
                             ntohs(dns_->ques_rrcount));
    SendDnsResponse();
    dns_proto->DelVmRequest(rkey_);
    return true;
}

void DnsHandler::DefaultDnsSendResponse() {
    agent()->GetDnsProto()->DelVmRequest(rkey_);
    if (dns_->flags.ret) {
        DNS_BIND_TRACE(DnsBindError, "Query failed : " << 
                       BindUtil::DnsResponseCode(dns_->flags.ret) <<
                       " xid = " << dns_->xid << " " <<
                       DnsItemsToString(items_) <<
                       DnsItemsToString(linklocal_items_));
    } else {
        DNS_BIND_TRACE(DnsBindTrace, "Query successful : xid = " <<
                       dns_->xid << " " << DnsItemsToString(items_) <<
                       DnsItemsToString(linklocal_items_));
    }
    SendDnsResponse();
}

bool DnsHandler::HandleVirtualDnsRequest(const VmInterface *vmitf) {
    rkey_ = new QueryKey(vmitf, dns_->xid);
    DnsProto *dns_proto = agent()->GetDnsProto();
    if (dns_proto->IsVmRequestDuplicate(rkey_)) {
        DNS_BIND_TRACE(DnsBindTrace,
                       "Retry DNS query from VM - dropping; interface = " <<
                       vmitf->vm_name() << " xid = " << dns_->xid << " " <<
                       DnsItemsToString(items_) <<
                       DnsItemsToString(linklocal_items_));
        dns_proto->IncrStatsRetransmitReq();
        return true;
    }
    dns_proto->AddVmRequest(rkey_);

    BindUtil::RemoveSpecialChars(ipam_type_.ipam_dns_server.
                                 virtual_dns_server_name);

    uint16_t ret = DNS_ERR_NO_ERROR;
    switch (dns_->flags.op) {
        case DNS_OPCODE_QUERY: {
            if (BindUtil::ParseDnsQuery((uint8_t *)dns_,
                                        pkt_info_->GetUdpPayloadLength(),
                                        &dns_resp_size_, items_) == false) {
                DNS_BIND_TRACE(DnsBindTrace, "vDNS query parsing failed; "
                               "interface = " << vmitf->vm_name() << " xid = " <<
                               dns_->xid);
                break;
            }
            resp_ptr_ = (uint8_t *)dns_ + dns_resp_size_;
            BindUtil::BuildDnsHeader(dns_, ntohs(dns_->xid), DNS_QUERY_RESPONSE, 
                                     DNS_OPCODE_QUERY, 0, 1, ret, 
                                     ntohs(dns_->ques_rrcount));
            // Check for linklocal service name resolution
            ResolveAllLinkLocalRequests();
            if (!items_.size()) {
                // no pending resolution, send response
                dns_->ans_rrcount = htons(dns_->ans_rrcount);
                SendDnsResponse();
                break;
            }
            UpdateQueryNames();

            uint8_t count = 0;
            bool query_success = false;

            action_ = DnsHandler::DNS_QUERY;
            while (count < dns_resolvers_.size()) {
                if (dns_resolvers_[count]) {
                    uint16_t xid = dns_proto->GetTransId();
                    if (SendDnsQuery(dns_resolvers_[count], xid) == true) {
                        dns_proto->AddDnsQueryIndex(xid, count);
                        query_success = true;
                    }
                }
                count++;
            }
            if (query_success) {
                // atleast one query sent succesful, do not delete request yet.
                return false;
            }
            break;
        }

        case DNS_OPCODE_UPDATE: {
            if (vdns_type_.dynamic_records_from_client) {
                DnsUpdateData *update_data = new DnsUpdateData();
                DnsProto::DnsUpdateIpc *update =
                    new DnsProto::DnsUpdateIpc(DnsAgentXmpp::Update, 
                                               update_data, vmitf, false);
                if (BindUtil::ParseDnsUpdate((uint8_t *)dns_,
                                             pkt_info_->GetUdpPayloadLength(),
                                             *update_data)) {
                    update_data->virtual_dns = ipam_type_.ipam_dns_server.
                                               virtual_dns_server_name;
                    Update(update);
                } else {
                    delete update;
                    ret = DNS_ERR_NO_IMPLEMENT;
                }
            } else {
                DNS_BIND_TRACE(DnsBindTrace, "Client not allowed to update "
                "dynamic records : " << vmitf->vm_name() << " ;Ignoring "
                "update for " << DnsItemsToString(items_));
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
                           << "from vm : " << vmitf->vm_name());
            break;
        }
    }

    dns_proto->DelVmRequest(rkey_);
    return true;
}

bool DnsHandler::SendDnsQuery(DnsResolverInfo *resolver,
                              uint16_t xid) {
    uint8_t *pkt = NULL;
    std::size_t len = 0;
    DnsProto *dns_proto = agent()->GetDnsProto();
    bool in_progress = dns_proto->IsDnsQueryInProgress(xid);
    if (in_progress) {
        if (resolver->retries_ >= dns_proto->max_retries()) {
            DNS_BIND_TRACE(DnsBindTrace, 
                           "Max retries reached for query; xid = " << xid <<
                           " " << DnsItemsToString(items_));
            goto cleanup;
        } else {
            resolver->retries_++;
        }
    } else {
        dns_proto->AddDnsQuery(xid, this);
    }

    pkt = new uint8_t[BindResolver::max_pkt_size];
    if (!default_method_) {
        len = BindUtil::BuildDnsQuery(pkt, xid,
                  ipam_type_.ipam_dns_server.virtual_dns_server_name, items_);
    } else {
        len = BindUtil::BuildDnsQuery(pkt, xid, "", items_);
    }
    if (BindResolver::Resolver()->DnsSend(pkt, resolver->ep_, len)) {
        DNS_BIND_TRACE(DnsBindTrace, "DNS query sent to named server : " <<
                       resolver->ep_.address().to_string() << "; xid =" <<
                       xid << " " << DnsItemsToString(items_));
        resolver->timer_->Cancel();
        resolver->timer_->Start(dns_proto->timeout(),
            boost::bind(&DnsHandler::TimerExpiry, this, xid));
        return true;
    }

cleanup:
    dns_proto->IncrStatsDrop();
    dns_proto->DelDnsQuery(xid);
    dns_proto->DelDnsQueryIndex(xid);
    return false;
}

// Check the request against configured link local services and
// update DnsItems, if found
bool DnsHandler::ResolveLinkLocalRequest(DnsItems::iterator &item,
                                         DnsItems *linklocal_items) const {
    GlobalVrouter *global_vrouter = agent_->oper_db()->global_vrouter();

    switch (item->type) {

    case DNS_A_RECORD: {
        std::string base_name;
        GetBaseName(item->name, &base_name);
        std::set<Ip4Address> service_ips;
        if (global_vrouter->FindLinkLocalService(item->name, &service_ips) ||
            (!base_name.empty() &&
             global_vrouter->FindLinkLocalService(base_name, &service_ips))) {
            for (std::set<Ip4Address>::iterator it = service_ips.begin();
                 it != service_ips.end(); ++it) {
                item->data = it->to_string();
                linklocal_items->push_back(*item);
            }
        }
        break;
    }

    case DNS_PTR_RECORD: {
        std::set<std::string> service_names;
        uint32_t addr;
        if (!BindUtil::GetAddrFromPtrName(item->name, addr)) {
            break;
        }
        if (global_vrouter->FindLinkLocalService(
                boost::asio::ip::address_v4(addr), &service_names)) {
            for (std::set<std::string>::iterator it = service_names.begin();
                 it != service_names.end(); ++it) {
                item->data = *it;
                linklocal_items->push_back(*item);
            }
        }
        break;
    }

    case DNS_AAAA_RECORD:
        break;

    default:
        break;

    }

    return (linklocal_items->size() > 0);
}

// Resolve link local service name requests locally
bool DnsHandler::ResolveAllLinkLocalRequests() {
    bool linklocal_request = false;
    for (DnsItems::iterator it = items_.begin(); it != items_.end(); ) {
        DnsItems linklocal_items;
        if (ResolveLinkLocalRequest(it, &linklocal_items)) {
            linklocal_request = true;
            for (DnsItems::iterator llit = linklocal_items.begin();
                 llit != linklocal_items.end(); ++llit) {
                resp_ptr_ = BindUtil::AddAnswerSection(resp_ptr_, *llit,
                                                       dns_resp_size_);
                dns_->ans_rrcount++;
            }
            // storing the link local items in a different list
            linklocal_items_.splice(linklocal_items_.begin(), linklocal_items);
            items_.erase(it++);
        } else {
            it++;
        }
    }
    return linklocal_request;
}

bool DnsHandler::HandleMessage() {
    switch (pkt_info_->ipc->cmd) {
        case DnsProto::DNS_DEFAULT_RESPONSE:
            return HandleDefaultDnsResponse();

        case DnsProto::DNS_BIND_RESPONSE:
            return HandleBindResponse();

        case DnsProto::DNS_TIMER_EXPIRED:
            return HandleRetryExpiry();

        case DnsProto::DNS_XMPP_SEND_UPDATE:
            return HandleUpdate();

        case DnsProto::DNS_XMPP_MODIFY_VDNS:
            return HandleModifyVdns();

        case DnsProto::DNS_XMPP_UPDATE_RESPONSE:
            return HandleUpdateResponse();

        case DnsProto::DNS_XMPP_SEND_UPDATE_ALL:
            return UpdateAll();

        default:
            DNS_BIND_TRACE(DnsBindError, "Invalid internal DNS message : " <<
                           pkt_info_->ipc->cmd);
            return true;
    }
}

bool DnsHandler::HandleDefaultDnsResponse() {
    DnsProto::DnsIpc *ipc = static_cast<DnsProto::DnsIpc *>(pkt_info_->ipc);
    ipc->handler->DefaultDnsSendResponse();
    delete ipc;
    return true;
}

bool DnsHandler::HandleBindResponse() {
    DnsProto::DnsIpc *ipc = static_cast<DnsProto::DnsIpc *>(pkt_info_->ipc);
    uint16_t xid = ntohs(*(uint16_t *)ipc->resp);
    DnsProto *dns_proto = agent()->GetDnsProto();
    DnsHandler *handler = dns_proto->GetDnsQueryHandler(xid);
    bool valid_response = false;
    if (handler) {
        dns_flags flags;
        DnsItems ques, ans, auth, add;
        if (BindUtil::ParseDnsResponse(ipc->resp, ipc->length, xid, flags,
                                       ques, ans, auth, add)) {
            switch(handler->action_) {
                case DnsHandler::DNS_QUERY:
                    if (flags.ret) {
                        DNS_BIND_TRACE(DnsBindError, "Query failed : " <<
                                       BindUtil::DnsResponseCode(flags.ret) <<
                                       " xid = " << xid << " " <<
                                       DnsItemsToString(items_) <<
                                       DnsItemsToString(linklocal_items_));
                    } else {
                        valid_response = true;
                        handler->Resolve(flags, ques, ans, auth, add);
                        DNS_BIND_TRACE(DnsBindTrace,
                                       "Query successful : xid = " <<
                                       xid << " " << DnsItemsToString(ans) <<
                                       DnsItemsToString(linklocal_items_));
                    }
                    break;

                default:
                    DNS_BIND_TRACE(DnsBindTrace,
                                   "Invalid DNS action: xid = " << xid);
                    break;
            }
        } else {
            DNS_BIND_TRACE(DnsBindTrace,
                           "Received invalid BIND response: xid = " << xid);
        }

        dns_proto->DelDnsQuery(xid);
        dns_proto->DelDnsQueryIndex(xid);

        if (valid_response) {
            dns_proto->DelDnsQueryHandler(handler);
            /* Delete Request on first valid Response from named Server */
            dns_proto->DelVmRequest(handler->rkey_);
            delete handler;
        } else {
            if (!handler->DefaultMethodInUse()) {
                if (!dns_proto->IsDnsHandlerInUse(handler)) {
                    HandleInvalidBindResponse(handler, flags, ques, ans, auth,
                                              add, xid);
                }
            } else {
                if (NeedRetryForNextServer(flags.ret)) {
                    /* Try sending query to next available server */
                    handler->IncrCurrIndex();
                    if (!handler->SendToDefaultServer()) {
                        /* Sending query to next server failed, send invalid
                         * response to client
                         */
                        HandleInvalidBindResponse(handler, flags, ques, ans,
                                                  auth, add, xid);
                    }
                } else {
                    /* Retry is not required, send invalid response to client */
                    HandleInvalidBindResponse(handler, flags, ques, ans, auth,
                                              add, xid);
                }
            }
        }
    } else {
        DNS_BIND_TRACE(DnsBindError, "Invalid or Response ignored xid " << xid <<
                       " received from DNS server - dropping");
        dns_proto->DelDnsQueryIndex(xid);
    }

    delete ipc;
    return true;
}

void DnsHandler::HandleInvalidBindResponse(DnsHandler *handler, dns_flags flags,
                                           const DnsItems &ques, DnsItems &ans,
                                           DnsItems &auth, DnsItems &add,
                                           uint16_t xid) {
    DnsProto *dns_proto = agent()->GetDnsProto();
    if (flags.ret) {
        /* Send last invalid response to requesting VM */
        handler->Resolve(flags, ques, ans, auth, add);
        DNS_BIND_TRACE(DnsBindTrace,
                       "Send invalid BIND response: xid = " << xid);
    } else {
        DNS_BIND_TRACE(DnsBindTrace,
                       "No response sent: xid = " << xid);
    }
    /* Delete Request on last invalid Response from named Server */
    dns_proto->DelVmRequest(handler->rkey_);
    delete handler;
}

bool DnsHandler::NeedRetryForNextServer(uint16_t code) {
   /*
    * Try next server for following response codes: server_failure(2),
    * Non-existent domain(3), Not implemented(4), Query refused(5),
    * Not Authorized(9)
    */
   if ((code > 1 && code < 6) || code == 9) {
       return true;
   }
   return false;
}

bool DnsHandler::SendToDefaultServer() {
    if (curr_index() > last_index()) {
        return false;
    }
    DnsProto *dns_proto = agent()->GetDnsProto();
    uint16_t xid = dns_proto->GetTransId();
    DnsResolverInfo *resolver;
    while (curr_index() <= last_index()) {
       resolver = def_dns_resolvers_[curr_index()];
       if (!SendDnsQuery(resolver, xid)) {
           IncrCurrIndex();
       } else {
           /* Query sent succesfully */
           return true;
       }
    }
    return false;
}

bool DnsHandler::HandleRetryExpiry() {
    DnsProto::DnsIpc *ipc = static_cast<DnsProto::DnsIpc *>(pkt_info_->ipc);
    DnsProto *dns_proto = agent()->GetDnsProto();
    uint16_t xid = ipc->xid;
    DnsHandler *handler = dns_proto->GetDnsQueryHandler(xid);
    if (handler) {
        DnsResolverInfo *resolver;
        if (handler->DefaultMethodInUse()) {
            resolver = handler->def_dns_resolvers_[handler->curr_index()];
            if (!handler->SendDnsQuery(resolver, xid)) {
                // Try sending query to next available server
                handler->IncrCurrIndex();
                if (!handler->SendToDefaultServer()) {
                    dns_proto->DelVmRequest(handler->rkey_);
                    delete handler;
                }
            }
        } else {
            uint16_t idx = dns_proto->GetDnsQueryServerIndex(ipc->xid);
            resolver = handler->dns_resolvers_[idx];
            if (!handler->SendDnsQuery(resolver, xid)) {
                if (!dns_proto->IsDnsHandlerInUse(handler)) {
                    dns_proto->DelVmRequest(handler->rkey_);
                    delete handler;
                }
            }
        }
    }
    delete ipc;
    return true;
}

bool DnsHandler::HandleUpdateResponse() {
    DnsProto::DnsUpdateIpc *ipc =
        static_cast<DnsProto::DnsUpdateIpc *>(pkt_info_->ipc);
    delete ipc;
    return true;
}

bool DnsHandler::HandleUpdate() {
    DnsProto::DnsUpdateIpc *ipc =
        static_cast<DnsProto::DnsUpdateIpc *>(pkt_info_->ipc);
    if (!ipc->xmpp_data) {
        DelUpdate(ipc);
    } else {
        Update(ipc);
    }
    return true;
}

bool DnsHandler::HandleModifyVdns() {
    DnsProto::DnsUpdateIpc *ipc =
        static_cast<DnsProto::DnsUpdateIpc *>(pkt_info_->ipc);
    DnsProto *dns_proto = agent()->GetDnsProto();
    std::vector<DnsProto::DnsUpdateIpc *> change_list;
    const DnsProto::DnsUpdateSet &update_set = dns_proto->update_set();
    for (DnsProto::DnsUpdateSet::const_iterator it = update_set.begin();
         it != update_set.end(); ++it) {
        if ((ipc->itf &&
             ((*it)->itf != ipc->itf || (*it)->floatingIp != ipc->floatingIp)) ||
            !((*it)->xmpp_data) || (*it)->xmpp_data->virtual_dns != ipc->old_vdns)
            continue;

        change_list.push_back(*it);
        // if the server ttl changes, we only readd with the new values
        if (!ipc->itf && ipc->new_vdns == ipc->old_vdns)
            continue;

        for (DnsItems::iterator item = (*it)->xmpp_data->items.begin(); 
             item != (*it)->xmpp_data->items.end(); ++item) {
            // in case of delete, set the class to NONE and ttl to 0
            (*item).eclass = DNS_CLASS_NONE;
            (*item).ttl = 0;
        }
        for (int i = 0; i < MAX_XMPP_SERVERS; i++) {
            AgentDnsXmppChannel *channel = agent()->dns_xmpp_channel(i);
            SendXmppUpdate(channel, (*it)->xmpp_data);
        }
    }
    for (unsigned int i = 0; i < change_list.size(); i++) {
        dns_proto->DelUpdateRequest(change_list[i]);
        if (ipc->new_vdns.empty())
            delete change_list[i];
    }
    if (ipc->new_vdns.empty())
        goto done;
    for (unsigned int i = 0; i < change_list.size(); i++) {
        change_list[i]->xmpp_data->virtual_dns = ipc->new_vdns;
        if (change_list[i]->xmpp_data->zone.find(".in-addr.arpa") ==
                                                 std::string::npos)
            change_list[i]->xmpp_data->zone = ipc->new_domain;
        for (DnsItems::iterator item = change_list[i]->xmpp_data->items.begin();
             item != change_list[i]->xmpp_data->items.end(); ++item) {
            (*item).eclass = DNS_CLASS_IN;
            (*item).ttl = ipc->ttl;
        }
        Update(change_list[i]);
    }

done:
    delete ipc;
    return true;
}

bool DnsHandler::UpdateAll() {
    DnsProto::DnsUpdateAllIpc *ipc =
        static_cast<DnsProto::DnsUpdateAllIpc *>(pkt_info_->ipc);
    const DnsProto::DnsUpdateSet &update_set =
        agent()->GetDnsProto()->update_set();
    for (DnsProto::DnsUpdateSet::const_iterator it = update_set.begin(); 
         it != update_set.end(); ++it) {
        SendXmppUpdate(ipc->channel, (*it)->xmpp_data);
    }

    delete ipc;
    return true;
}

void DnsHandler::SendXmppUpdate(AgentDnsXmppChannel *channel, 
                                DnsUpdateData *xmpp_data) {
    if (channel && agent_->is_dns_xmpp_channel(channel)) {
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

            uint8_t data[DnsAgentXmpp::max_dns_xmpp_msg_len];
            xid_ = agent()->GetDnsProto()->GetTransId();
            std::size_t len = 0;
            if ((len = DnsAgentXmpp::DnsAgentXmppEncode(channel->GetXmppChannel(), 
                                                        DnsAgentXmpp::Update,
                                                        xid_, 0, xmpp_data, 
                                                        data)) > 0) {
                channel->SendMsg(data, len);
            }

            done.splice(done.end(), xmpp_data->items, xmpp_data->items.begin(),
                        xmpp_data->items.end());
        }
        xmpp_data->items.swap(done);
    }
}

void 
DnsHandler::Resolve(dns_flags flags, const DnsItems &ques, DnsItems &ans,
                    DnsItems &auth, DnsItems &add) {
    for (DnsItems::iterator it = ans.begin(); it != ans.end(); ++it) {
        // find the matching entry in the request
        bool name_update_required = true;
        for (DnsItems::const_iterator item = items_.begin();
             item != items_.end(); ++item) {
            if (it->name == item->name && it->eclass == item->eclass) {
                it->name_plen = item->name_plen;
                it->name_offset = item->offset;
                name_update_required = false;
                break;
            }
        }
        UpdateOffsets(*it, name_update_required);
        resp_ptr_ = BindUtil::AddAnswerSection(resp_ptr_, *it, dns_resp_size_);
        dns_->ans_rrcount++;
    }

    for (DnsItems::iterator it = auth.begin(); it != auth.end(); ++it) {
        UpdateOffsets(*it, true);
        UpdateGWAddress(*it);
        resp_ptr_ = BindUtil::AddAnswerSection(resp_ptr_, *it, dns_resp_size_);
        dns_->auth_rrcount++;
    }

    for (DnsItems::iterator it = add.begin(); it != add.end(); ++it) {
        UpdateOffsets(*it, true);
        UpdateGWAddress(*it);
        resp_ptr_ = BindUtil::AddAnswerSection(resp_ptr_, *it, dns_resp_size_);
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
    PktInfo in_pkt_info = *pkt_info_.get();

    uint16_t buff_len = in_pkt_info.packet_buffer()->buffer_len();
    pkt_info_->AllocPacketBuffer(agent(), PktHandler::DNS, buff_len, 0);
    char *buff = (char *)pkt_info_->packet_buffer()->data();
    memset(buff, 0, buff_len);
    pkt_info_->vrf = in_pkt_info.vrf;

    uint16_t eth_type = ETHERTYPE_IP;
    if (in_pkt_info.ip == NULL)
        eth_type = ETHERTYPE_IPV6;

    MacAddress dest_mac(in_pkt_info.eth->ether_shost);
    pkt_info_->eth = (struct ether_header *)(buff);
    uint16_t eth_len = 0;
    eth_len += EthHdr(buff, buff_len, in_pkt_info.GetAgentHdr().ifindex,
                      agent()->vhost_interface()->mac(), dest_mac, eth_type);

    uint16_t data_len = dns_resp_size_;
    // fill in the response
    if (in_pkt_info.ip) {
        // IPv4 request
        in_addr_t src_ip = in_pkt_info.ip->ip_dst.s_addr;
        in_addr_t dest_ip = in_pkt_info.ip->ip_src.s_addr;

        pkt_info_->ip = (struct ip *)(buff + eth_len);
        pkt_info_->transp.udp = (struct udphdr *)
            ((uint8_t *)pkt_info_->ip + sizeof(struct ip));

        data_len += sizeof(udphdr);
        UdpHdr(data_len, src_ip, DNS_SERVER_PORT,
               dest_ip, ntohs(in_pkt_info.transp.udp->uh_sport));
        data_len += sizeof(struct ip);
        IpHdr(data_len, src_ip, dest_ip, IPPROTO_UDP,
              DEFAULT_IP_ID, DEFAULT_IP_TTL);

    } else {
        // IPv6 request
        Ip6Address src_ip = in_pkt_info.ip_daddr.to_v6();
        Ip6Address dest_ip = in_pkt_info.ip_saddr.to_v6();

        pkt_info_->ip6 = (struct ip6_hdr *)(buff + eth_len);
        pkt_info_->transp.udp = (struct udphdr *)
            ((uint8_t *)pkt_info_->ip6 + sizeof(struct ip6_hdr));

        data_len += sizeof(udphdr);
        UdpHdr(data_len, src_ip.to_bytes().data(),
               DNS_SERVER_PORT, dest_ip.to_bytes().data(),
               ntohs(in_pkt_info.transp.udp->uh_sport), IPPROTO_UDP);

        data_len += sizeof(struct ip6_hdr);
        Ip6Hdr(pkt_info_->ip6, data_len, IPPROTO_UDP, 64,
               src_ip.to_bytes().data(), dest_ip.to_bytes().data());
    }

    memcpy(((char *)pkt_info_->transp.udp + sizeof(udphdr)),
           ((char *)in_pkt_info.transp.udp + sizeof(udphdr)),
           dns_resp_size_);

    dns_resp_size_ += data_len + eth_len;
    pkt_info_->set_len(dns_resp_size_);

    PacketInterfaceKey key(nil_uuid(), agent()->pkt_interface_name());
    Interface *pkt_itf = static_cast<Interface *>
                         (agent()->interface_table()->FindActiveEntry(&key));
    if (pkt_itf) {
        UpdateStats();
        uint32_t interface =
            (pkt_info_->agent_hdr.cmd == AgentHdr::TRAP_TOR_CONTROL_PKT) ?
            pkt_info_->agent_hdr.cmd_param : GetInterfaceIndex();
        uint16_t command =
            (pkt_info_->agent_hdr.cmd == AgentHdr::TRAP_TOR_CONTROL_PKT) ?
            (uint16_t)AgentHdr::TX_ROUTE : AgentHdr::TX_SWITCH;
        Send(interface, pkt_info_->vrf, command, PktHandler::DNS);
    } else {
        agent()->GetDnsProto()->IncrStatsDrop();
    }
}

void DnsHandler::UpdateQueryNames() {
    for (DnsItems::iterator it = items_.begin(); it != items_.end(); ++it) {
        if (it->name.find('.', 0) == std::string::npos) {
            it->name.append(".");
            it->name.append(vdns_type_.domain_name);
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
    if ((item.type == DNS_A_RECORD || item.type == DNS_AAAA_RECORD) &&
        (item.data == agent()->dns_server(0) ||
         item.data == agent()->dns_server(1))) {
        item.data = pkt_info_->ip_daddr.to_string(ec);
    }
}

void DnsHandler::Update(InterTaskMsg *msg) {
    DnsProto::DnsUpdateIpc *update = static_cast<DnsProto::DnsUpdateIpc *>(msg);
    bool free_update = true;
    DnsProto *dns_proto = agent()->GetDnsProto();
    DnsProto::DnsUpdateIpc *update_req = dns_proto->FindUpdateRequest(update);
    if (update_req) {
        DnsUpdateData *data = update_req->xmpp_data;
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
            dns_proto->DelUpdateRequest(update_req);
            delete update_req;
        }
        if (!update->xmpp_data->items.size())
            goto done;
    } else {
        dns_proto->AddUpdateRequest(update);
        free_update = false;
    }

    for (int i = 0; i < MAX_XMPP_SERVERS; i++) {
        AgentDnsXmppChannel *channel = agent()->dns_xmpp_channel(i);
        SendXmppUpdate(channel, update->xmpp_data);
    }

done:
    if (free_update)
        delete update;
}

void DnsHandler::DelUpdate(InterTaskMsg *msg) {
    DnsProto::DnsUpdateIpc *update = static_cast<DnsProto::DnsUpdateIpc *>(msg);
    DnsProto *dns_proto = agent()->GetDnsProto();
    DnsProto::DnsUpdateIpc *update_req = dns_proto->FindUpdateRequest(update);
    while (update_req) {
        for (DnsItems::iterator item = update_req->xmpp_data->items.begin(); 
             item != update_req->xmpp_data->items.end(); ++item) {
            // in case of delete, set the class to NONE and ttl to 0
            (*item).eclass = DNS_CLASS_NONE;
            (*item).ttl = 0;
        }
        for (int i = 0; i < MAX_XMPP_SERVERS; i++) {
            AgentDnsXmppChannel *channel = 
                        agent()->dns_xmpp_channel(i);
            SendXmppUpdate(channel, update_req->xmpp_data);
        }
        dns_proto->DelUpdateRequest(update_req);
        delete update_req;
        update_req = dns_proto->FindUpdateRequest(update);
    }
    delete update;
}

void DnsHandler::UpdateStats() {
    DnsProto *dns_proto = agent()->GetDnsProto();
    switch (dns_->flags.ret) {
        case DNS_ERR_NO_ERROR:
            dns_proto->IncrStatsRes();
            break;

        case DNS_ERR_NO_IMPLEMENT:
            dns_proto->IncrStatsUnsupp();
            break;

        case DNS_ERR_FORMAT_ERROR:
        case DNS_ERR_NO_SUCH_NAME:
        case DNS_ERR_SERVER_FAIL:
        case DNS_ERR_NOT_AUTH:
        default:
            dns_proto->IncrStatsFail();
            break;
    }
}

bool DnsHandler::TimerExpiry(uint16_t xid) {
    agent()->GetDnsProto()->SendDnsIpc(DnsProto::DNS_TIMER_EXPIRED, xid,
                                       NULL, NULL);
    return false;
}

void DnsHandler::GetDomainName(const VmInterface *vm_itf,
                               std::string *domain_name) const {
    std::vector<autogen::DhcpOptionType> options;
    if (vm_itf->GetInterfaceDhcpOptions(&options)) {
        if (GetDomainNameFromDhcp(options, domain_name))
            return;
    }

    if (pkt_info_->ip_saddr.is_v4()) {
        if (vm_itf->GetSubnetDhcpOptions(&options, false) ||
            vm_itf->GetIpamDhcpOptions(&options, false)) {
            GetDomainNameFromDhcp(options, domain_name);
            return;
        }
    }

    if (pkt_info_->ip_saddr.is_v6()) {
        if (vm_itf->GetSubnetDhcpOptions(&options, true) ||
            vm_itf->GetIpamDhcpOptions(&options, true)) {
            GetDomainNameFromDhcp(options, domain_name);
            return;
        }
    }
}

bool DnsHandler::GetDomainNameFromDhcp(
                    std::vector<autogen::DhcpOptionType> &options,
                    std::string *domain_name) const {
    for (unsigned int i = 0; i < options.size(); ++i) {
        uint32_t option_type;
        std::stringstream str(options[i].dhcp_option_name);
        str >> option_type;
        if (option_type == DHCP_OPTION_DOMAIN_NAME) {
            *domain_name = options[i].dhcp_option_value;
            return true;
        }
    }
    return false;
}

// remove domain name suffix from given name, if present
void DnsHandler::GetBaseName(const std::string &name, std::string *base) const {
    if (domain_name_.empty())
        return;

    std::size_t pos = name.find(domain_name_, 1);
    while (pos != std::string::npos) {
        std::string base_name = name.substr(0, pos - 1);
        if (name == base_name + "." + domain_name_ ||
            name == base_name + "." + domain_name_ + ".") {
            *base = base_name;
            return;
        }
        pos = name.find(domain_name_, pos + 1);
    }
}

std::string DnsHandler::DnsItemsToString(DnsItems &items) const {
    std::string str;
    for (DnsItems::const_iterator it = items.begin(); it != items.end(); ++it) {
        str.append(it->ToString());
        str.append(" ");
    }
    return str;
}
