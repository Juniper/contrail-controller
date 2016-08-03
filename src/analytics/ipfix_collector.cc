/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "db_handler.h"
#include "ipfix_collector.h"
#include "ipfix_col.h"
#include "uflow_constants.h"
#include "uflow_types.h"

#include <boost/assign/list_of.hpp>
#include <stdio.h>

using std::string;
using std::map;

static map<string,string> uflowfields_ = boost::assign::map_list_of(
     "protocolIdentifier", "protocol")(
     "sourceTransportPort", "sport")(
     "destinationTransportPort", "dport")(
     "sourceIPv4Address","sip")(
     "sourceIPv6Address","sip")(
     "destinationIPv4Address","dip")(
     "destinationIPv6Address","dip")(
     "vlanId","vlan")(
     "ingressInterface","pifindex");


void IpfixCollector::HandleReceive(const boost::asio::const_buffer& buffer,
            boost::asio::ip::udp::endpoint remote_endpoint,
            size_t bytes_transferred,
            const boost::system::error_code& error) {
    if (!error) {
        ProcessIpfixPacket(buffer, bytes_transferred, remote_endpoint); 
    }
    DeallocateBuffer(buffer);
}

IpfixCollector::IpfixCollector(EventManager* evm,
            DbHandlerPtr db_handler,
            string ip_address,
            int port) 
    : UdpServer(evm),
      db_handler_(db_handler),
      ip_address_(ip_address),
      port_(port),
      num_packets_(0),
      udp_sources_(NULL),
      colinfo_(new ipfix_col_info())  {
}

IpfixCollector::~IpfixCollector() {
}

void IpfixCollector::Start() {
    if (port_ != -1) {
        if (ip_address_.empty()) {
            Initialize(port_);
        } else {
            Initialize(ip_address_, port_);
        }
        StartReceive();
        //(void) ipfix_col_start_msglog( stdout );
        if ( ipfix_init() <0 ) {
            fprintf( stderr, "ipfix_init() failed: %s\n", strerror(errno) );
            exit(1);
        }
        if (RegisterCb() <0 ) {
            fprintf( stderr, "RegisterCb() failed: %s\n", strerror(errno) );
            exit(1);
        }
    }
}

void IpfixCollector::Shutdown() {
    //(void) ipfix_col_stop_msglog();
    if (port_ != -1) {
	    (void) ipfix_col_cancel_export(colinfo_.get());
	    ipfix_cleanup();
	    UdpServer::Shutdown();
    }
}

void IpfixCollector::ProcessIpfixPacket(const boost::asio::const_buffer& buffer,
                                        size_t length,
                                        boost::asio::ip::udp::endpoint generator_ip) { 
    num_packets_++;

    ipfix_input_t            input;

    input.type = IPFIX_INPUT_IPCON;
    input.u.ipcon.addr = (struct sockaddr *) generator_ip.data();
    input.u.ipcon.addrlen = generator_ip.size();
    (void) ipfix_parse_msg( &input, &udp_sources_,
        boost::asio::buffer_cast<const unsigned char*>(buffer), length);
}

int IpfixCollector::NewSource(ipfixs_node *s, void *arg)
{
    LOG(DEBUG,"New IPFIX source: " << ipfix_col_input_get_ident( s->input ) << "/" <<
          (u_long)s->odid );
    return 0;
}

int IpfixCollector::NewMsg(ipfixs_node *s,
        ipfix_hdr_t *hdr, void *arg )
{
#if 0
    LOG(DEBUG,"IPFIX-HDR: version= " << hdr->version <<
        " seqno= " << (u_long)hdr->seqno << "  sourceid= " <<
        (u_long)hdr->sourceid);
#endif
    return 0;
}

int IpfixCollector::ExportTrecord(ipfixs_node *s, ipfixt_node *t, void *arg)
{
#if 0
    LOG(DEBUG,"TEMPLATE RECORD: " << t->ipfixt->tid <<
        " template id: " << t->ipfixt->tid <<
        ((t->ipfixt->nscopefields)?"(option template)":"") <<
        " nfields: " << t->ipfixt->nfields);
#endif
    return 0;
}

int IpfixCollector::ExportDrecord(
    ipfixs_node      *s,
    ipfixt_node      *t,
    ipfix_datarecord *data,
    void             *arg)
{
#if 0
    LOG(DEBUG,"DATA RECORD: template id: " << t->ipfixt->tid <<
        " nfields: " << t->ipfixt->nfields <<
        ((t->ipfixt->nscopefields)?"(option record)":""));
#endif
    UFlowData flow_data;
    flow_data.set_name(ipfix_col_input_get_ident(s->input));
    // TODO: Get the timestamp from the packet
    uint64_t tm = UTCTimestampUsec();
    std::vector<UFlowSample> samples;
    UFlowSample sample;
    sample.set_flowtype(g_uflow_constants.FlowTypeName.find(
                            FlowType::IPFIX)->second);
    for (int i=0; i<t->ipfixt->nfields; i++ ) {
        const char * kbuf = t->ipfixt->fields[i].elem->ft->name;
        char tmpbuf[128];
        t->ipfixt->fields[i].elem->snprint( tmpbuf, sizeof(tmpbuf), 
                                            data->addrs[i], data->lens[i] );
        if (!strcmp(kbuf, "protocolIdentifier")) {
            sample.protocol = atoi(tmpbuf);
        } else if (!strcmp(kbuf,"sourceTransportPort")) {
            sample.sport = atoi(tmpbuf);
        } else if (!strcmp(kbuf, "destinationTransportPort")) {
            sample.dport = atoi(tmpbuf);
        } else if (!strcmp(kbuf, "sourceIPv4Address")) {
            sample.sip = tmpbuf;
        } else if (!strcmp(kbuf, "sourceIPv6Address")) {
            sample.sip = tmpbuf;
        } else if (!strcmp(kbuf, "destinationIPv4Address")) {
            sample.dip = tmpbuf;
        } else if (!strcmp(kbuf, "destinationIPv6Address")) {
            sample.dip = tmpbuf;
        } else if (!strcmp(kbuf, "ingressInterface")) {
            sample.pifindex = atoi(tmpbuf);
        } else if (!strcmp(kbuf, "vlanId")) {
            sample.vlan = atoi(tmpbuf);
        } else {
            // TODO : Put other fields in  "otherinfo"
        }
    }
    samples.push_back(sample);
    flow_data.set_flow(samples);
    db_handler_->UnderlayFlowSampleInsert(flow_data, tm,
        GenDb::GenDbIf::DbAddColumnCb());
    return 0;
}

void IpfixCollector::ExportCleanup(void *arg)
{
    return;
}

static int NewSource(ipfixs_node *s, void *arg) {
    IpfixCollector* th = static_cast<IpfixCollector*>(arg);
    return th->NewSource(s,arg);
}

static int NewMsg(ipfixs_node *s, 
        ipfix_hdr_t *hdr, void *arg) {
    IpfixCollector* th = static_cast<IpfixCollector*>(arg);
    return th->NewMsg(s,hdr,arg);
}

static int ExportDrecord(
            ipfixs_node *s,
            ipfixt_node *t,
            ipfix_datarecord *data,
            void *arg) {
    IpfixCollector* th = static_cast<IpfixCollector*>(arg);
    return th->ExportDrecord(s,t,data,arg);
}

static int ExportTrecord(ipfixs_node *s, ipfixt_node *t, void *arg) {
    IpfixCollector* th = static_cast<IpfixCollector*>(arg);
    return th->ExportTrecord(s,t,arg);
}

static void ExportCleanup(void *arg) {
    IpfixCollector* th = static_cast<IpfixCollector*>(arg);
    return th->ExportCleanup(arg);
}

int IpfixCollector::RegisterCb(void)
{
    colinfo_->export_newsource = &::NewSource;
    colinfo_->export_newmsg = &::NewMsg;
    colinfo_->export_trecord = &::ExportTrecord;
    colinfo_->export_drecord = &::ExportDrecord;
    colinfo_->export_cleanup = &::ExportCleanup;
    colinfo_->data = (void*)this;

    return ipfix_col_register_export(colinfo_.get());
}
