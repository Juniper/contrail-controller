/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <pkt/proto_handler.h>
#include <diag/diag_pkt_handler.h>
#include <diag/diag_proto.h>
#include <diag/segment_health_check.h>
#include <oper/health_check.h>
#include <oper/metadata_ip.h>

SegmentHealthCheckPkt::SegmentHealthCheckPkt(HealthCheckInstanceService *svc,
                                             DiagTable *diag_table) :
    DiagEntry(svc->ip()->service_ip().to_string(),
              svc->ip()->destination_ip().to_string(), IPPROTO_ICMP, 0, 0, "",
              svc->service()->timeout() * 1000, svc->service()->max_retries(),
              diag_table), service_(svc), state_(SUCCESS) {
}

SegmentHealthCheckPkt::~SegmentHealthCheckPkt() {
}

void SegmentHealthCheckPkt::FillDiagHeader(AgentDiagPktData *data) const {
    data->op_ = htonl(AgentDiagPktData::DIAG_REQUEST);
    data->key_ = htons(key_);
    data->seq_no_ = htonl(seq_no_);
}

void SegmentHealthCheckPkt::SendRequest() {
    Agent *agent = diag_table_->agent();

    //Allocate buffer to hold packet
    boost::shared_ptr<PktInfo> pkt_info(new PktInfo(agent, kBufferSize,
                                                    PktHandler::DIAG, 0));
    uint8_t *msg = pkt_info->packet_buffer()->data();
    memset(msg, 0, kBufferSize);

    DiagPktHandler *pkt_handler =
        new DiagPktHandler(agent, pkt_info,
                           *(agent->event_manager())->io_service());
    uint16_t len = sizeof(AgentDiagPktData);
    uint8_t *data = NULL;
    if (sip_.is_v4()) {
        //Update pointers to ethernet header, ip header and l4 header
        pkt_info->UpdateHeaderPtr();
        switch (proto_) {
            case IPPROTO_ICMP:
                len += 8;
                data = (uint8_t *)pkt_handler->pkt_info()->transp.icmp + 8;
                pkt_handler->pkt_info()->transp.icmp->icmp_type = ICMP_ECHO;
                pkt_handler->pkt_info()->transp.icmp->icmp_code = 0;
                pkt_handler->pkt_info()->transp.icmp->icmp_cksum = 0xffff;
                break;
            default:
                assert(0);
        }
        //Add Diag header as ICMP payload
        FillDiagHeader((AgentDiagPktData *)data);
        len += sizeof(struct ip);
        pkt_handler->IpHdr(len, ntohl(sip_.to_v4().to_ulong()),
                           ntohl(dip_.to_v4().to_ulong()),
                           proto_, DEFAULT_IP_ID, DEFAULT_IP_TTL);
        len += sizeof(ether_header);
        pkt_handler->EthHdr(agent->vhost_interface()->mac(),
                            agent->vrrp_mac(), ETHERTYPE_IP);
    } else {
        //TODO: support for IPv6
        assert(0);
    }
    //Increment the attempt count
    seq_no_++;

    //Send request out
    pkt_handler->pkt_info()->set_len(len);
    pkt_handler->Send(service_->interface()->id(),
                      service_->interface()->vrf_id(), AgentHdr::TX_SWITCH,
                      CMD_PARAM_PACKET_CTRL, CMD_PARAM_1_DIAG,
                      PktHandler::DIAG);
    delete pkt_handler;
    diag_table_->diag_proto()->IncrementDiagStats(service_->interface()->id(),
                                                  DiagProto::REQUESTS_SENT);
    return;
}

void SegmentHealthCheckPkt::RequestTimedOut(uint32_t seqno) {
    if (seq_no_ >= GetMaxAttempts()) {
        Notify(FAILURE);
        seq_no_ = 0;
    }
}

void SegmentHealthCheckPkt::HandleReply(DiagPktHandler *handler) {
    diag_table_->diag_proto()->IncrementDiagStats(service_->interface()->id(),
                                                  DiagProto::REPLIES_RECEIVED);
    seq_no_ = 0;
    Notify(SUCCESS);
}

void SegmentHealthCheckPkt::Notify(Status status) {
    if (state_ != status) {
        state_ = status;
        std::string data = (state_ == SUCCESS) ? "success" : "failure";
        service_->OnRead(data);
    }
}
