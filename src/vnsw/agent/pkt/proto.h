/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_proto_hpp
#define vnsw_agent_proto_hpp

#include <net/if.h>
#include "base/queue_task.h"
#include "vr_defs.h"
#include "pkt_handler.h"
#include "oper/mirror_table.h"

template <class Handler>
class Proto {
public:
    static void Init(const char *name, PktHandler::ModuleName mod, 
                     boost::asio::io_service &io) {
        assert(instance_ == NULL);
        instance_ = new Proto<Handler>(name, mod, io);
    };

    static void Shutdown() {
        delete instance_;
        instance_ = NULL;
    }

    virtual ~Proto() { 
        work_queue_. Shutdown();
    };

    bool EnqueueMessage(PktInfo *msg) {
        return work_queue_.Enqueue(msg);
    };

    bool ProcessProto(PktInfo *msg_info) {
        Handler *handler = new Handler(msg_info, io_);
        if (handler->Run())
            delete handler;
        return true;
    }

protected:
    Proto(const char *task_name, PktHandler::ModuleName mod, boost::asio::io_service &io) : 
        work_queue_(TaskScheduler::GetInstance()->GetTaskId(task_name), mod,
                    boost::bind(&Proto<Handler>::ProcessProto, this, _1)), io_(io) { 
        PktHandler::GetPktHandler()->Register(mod,
                    boost::bind(&Proto::EnqueueMessage, this, _1) );
    };

    static Proto *instance_;
    WorkQueue<PktInfo *> work_queue_;
    boost::asio::io_service &io_;
    DISALLOW_COPY_AND_ASSIGN(Proto);
};

// Pseudo header for UDP checksum
struct PseudoUdpHdr {
    in_addr_t src;
    in_addr_t dest;
    uint8_t   res;
    uint8_t   prot;
    uint16_t  len;
    PseudoUdpHdr(in_addr_t s, in_addr_t d, uint8_t p, uint16_t l) : 
        src(s), dest(d), res(0), prot(p), len(l) { };
};

// Pseudo header for TCP checksum
struct PseudoTcpHdr {
    in_addr_t src;
    in_addr_t dest;
    uint8_t   res;
    uint8_t   prot;
    uint16_t  len;
    PseudoTcpHdr(in_addr_t s, in_addr_t d, uint16_t l) : 
        src(s), dest(d), res(0), prot(6), len(l) { };
};

// Protocol handler
class ProtoHandler {
public:
    ProtoHandler(PktInfo *info, boost::asio::io_service &io) : 
        pkt_info_(info), io_(io) {};
    ProtoHandler(boost::asio::io_service &io) : io_(io) {
        pkt_info_ = new PktInfo();
    };
    virtual ~ProtoHandler() { 
        // pkt_info_->pkt.reset();
        delete pkt_info_;
    };

    virtual bool Run() = 0;

    void Send(uint16_t, uint16_t, uint16_t, uint16_t, PktHandler::ModuleName);

    void EthHdr(const unsigned char *, const unsigned char *, const uint16_t);
    void IpHdr(uint16_t, in_addr_t, in_addr_t, uint8_t);
    void UdpHdr(uint16_t, in_addr_t, uint16_t, in_addr_t, uint16_t);
    void TcpHdr(in_addr_t, uint16_t, in_addr_t, uint16_t, bool , uint32_t, uint16_t);
    uint32_t Sum(uint16_t *, std::size_t, uint32_t);
    uint16_t Csum(uint16_t *, std::size_t, uint32_t);
    uint16_t UdpCsum(in_addr_t, in_addr_t, std::size_t, udphdr *);
    uint16_t TcpCsum(in_addr_t, in_addr_t, uint16_t , tcphdr *);
    void Swap();
    void SwapL4();
    void SwapIpHdr();
    void SwapEthHdr();
    uint32_t GetVrf() { return pkt_info_->GetAgentHdr().vrf;};
    uint16_t GetIntf() { return pkt_info_->GetAgentHdr().ifindex;};
    uint16_t GetLen() { return pkt_info_->len;};
    uint32_t GetCmdParam() { return pkt_info_->GetAgentHdr().cmd_param;};

protected:
    PktInfo *pkt_info_;
    boost::asio::io_service &io_;

private:
};

#endif // vnsw_agent_proto_hpp
