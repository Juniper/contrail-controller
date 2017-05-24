/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef _dns_agent_xmpp_channel_h_
#define _dns_agent_xmpp_channel_h_

#include <map>
#include <set>
#include <string>
#include <boost/function.hpp>
#include <boost/bind.hpp>

#include "xmpp/xmpp_channel.h"
#include "bind/bind_util.h"

class XmppServer;
class DnsAgentXmppChannelManager;
class AgentData;
class AgentDnsData;
class TaskTrigger;

class DnsAgentXmppChannel {
public:
    typedef std::set<DnsUpdateData *, DnsUpdateData::Compare> DataSet;

    DnsAgentXmppChannel(XmppChannel *channel,
                        DnsAgentXmppChannelManager *mgr);
    virtual ~DnsAgentXmppChannel();
    void Close();
    void ReceiveReq(const XmppStanza::XmppMessage *msg);
    void GetAgentDnsData(AgentDnsData &data);
    void UpdateDnsRecords(BindUtil::Operation op);

private:
    std::string GetDnsRecordName(std::string &vdns_name, const DnsItem &item);
    void HandleAgentUpdate(std::auto_ptr<DnsUpdateData> rcv_data);

    DataSet update_data_;
    XmppChannel *channel_;
    DnsAgentXmppChannelManager *mgr_;
};

class DnsAgentXmppChannelManager {
public:
    struct RecordRequest {
        BindUtil::Operation op;
        std::string record_name;
        std::string vdns_name;
        DnsItem item;

        RecordRequest(BindUtil::Operation o, const std::string &rec,
                      const std::string &vdns, const DnsItem &it)
            : op(o), record_name(rec), vdns_name(vdns), item(it) {}
    };
    typedef WorkQueue<boost::shared_ptr<RecordRequest> > RecordRequestWorkQueue;

    typedef std::map<const XmppChannel *, DnsAgentXmppChannel *> ChannelMap;

    DnsAgentXmppChannelManager(XmppServer *server);
    virtual ~DnsAgentXmppChannelManager();
    void RemoveChannel(XmppChannel *ch);
    DnsAgentXmppChannel *FindChannel(const XmppChannel *ch);
    void HandleXmppChannelEvent(XmppChannel *channel, xmps::PeerState state);
    bool ProcessRecord(boost::shared_ptr<RecordRequest> req);
    void EnqueueRecord(boost::shared_ptr<RecordRequest> req);

    void GetAgentData(std::vector<AgentData> &list);
    void GetAgentDnsData(std::vector<AgentDnsData> &dt);

private:
    uint8_t ChannelToDscp(const XmppChannel *xc) const;
    XmppServer *server_;
    ChannelMap channel_map_;
    tbb::mutex mutex_;
    RecordRequestWorkQueue work_queue_;
};

#endif // _dns_agent_xmpp_channel_h_
