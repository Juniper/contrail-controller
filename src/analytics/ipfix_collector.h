/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __IPFIX_COLLECTOR_H__
#define __IPFIX_COLLECTOR_H__

#include <boost/ptr_container/ptr_map.hpp>

#include "io/udp_server.h"
#include "db_handler.h"
#include "ipfix.h"

struct ipfixs_node;
struct ipfixt_node;
struct ipfix_col_info;
struct ipfix_datarecord;

class IpfixCollector : public UdpServer {
public:
    explicit IpfixCollector(EventManager* evm,
        DbHandlerPtr db_handler, std::string ip_address,
        int port);
    ~IpfixCollector();
    virtual void Start();
    virtual void Shutdown();

    int NewSource(ipfixs_node *s, void *arg);
    int NewMsg(ipfixs_node *s, 
        ipfix_hdr_t *hdr, void *arg);
    void ExportCleanup(void *arg);
    int ExportDrecord(
            ipfixs_node *s,
            ipfixt_node *t,
            ipfix_datarecord *data,
            void *arg);
    int ExportTrecord(ipfixs_node *s, ipfixt_node *t, void *arg);
private:
   
    DbHandlerPtr db_handler_;
    std::string ip_address_;
    int port_;
    uint64_t num_packets_;
    ipfixs_node  *udp_sources_;
    std::map<std::string,std::string> uflowfields_;
    boost::scoped_ptr<ipfix_col_info> colinfo_;

    void HandleReceive(const boost::asio::const_buffer& buffer,
                       boost::asio::ip::udp::endpoint remote_endpoint,
                       size_t bytes_transferred,
                       const boost::system::error_code& error);
    void ProcessIpfixPacket(const boost::asio::const_buffer& buffer,
                            size_t length,
                            boost::asio::ip::udp::endpoint generator_ip);

    int RegisterCb(void);

    DISALLOW_COPY_AND_ASSIGN(IpfixCollector);
};

#endif // __IPFIX_COLLECTOR_H__
