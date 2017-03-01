/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __VNSW_CONTROLLER_INIT_HPP__
#define __VNSW_CONTROLLER_INIT_HPP__

#include <sandesh/sandesh_trace.h>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <controller/controller_timer.h>
#include "xmpp/xmpp_channel.h"
#include <oper/peer.h>

class AgentXmppChannel;
class AgentDnsXmppChannel;
class AgentIfMapVmExport;
class BgpPeer;
class XmlBase;
class XmppChannelConfig;

class ControllerWorkQueueData {
public:
    ControllerWorkQueueData() {}
    virtual ~ControllerWorkQueueData() {}

private:
    DISALLOW_COPY_AND_ASSIGN(ControllerWorkQueueData);
};

class ControllerXmppData : public ControllerWorkQueueData {
public:
    ControllerXmppData(xmps::PeerId peer_id, xmps::PeerState peer_state,
                       uint8_t channel_id, std::auto_ptr<XmlBase> dom,
                       bool config) :
        ControllerWorkQueueData(),
        peer_id_(peer_id), peer_state_(peer_state), channel_id_(channel_id),
        dom_(dom), config_(config) { }
    virtual ~ControllerXmppData() { }

    xmps::PeerId peer_id() const {return peer_id_;}
    xmps::PeerState peer_state() const {return peer_state_;}
    uint8_t channel_id() const {return channel_id_;}
    std::auto_ptr<XmlBase> dom() {return dom_;}
    bool config() const {return config_;}

private:
    xmps::PeerId peer_id_;
    xmps::PeerState peer_state_;
    uint8_t channel_id_;
    std::auto_ptr<XmlBase> dom_;
    bool config_;
    DISALLOW_COPY_AND_ASSIGN(ControllerXmppData);
};

class ControllerVmiSubscribeData : public ControllerWorkQueueData {
public:
    ControllerVmiSubscribeData(bool del, const boost::uuids::uuid &vmi_uuid,
                               const boost::uuids::uuid &vm_uuid) :
        del_(del), vmi_uuid_(vmi_uuid), vm_uuid_(vm_uuid) { }
    virtual ~ControllerVmiSubscribeData() {}

    bool del_;
    boost::uuids::uuid vmi_uuid_;
    boost::uuids::uuid vm_uuid_;
};

class ControllerReConfigData : public ControllerWorkQueueData {
public:
    ControllerReConfigData(std::string service_name, std::vector<string> server_list);
    virtual ~ControllerReConfigData() {}

    std::string service_name_;
    std::vector<string> server_list_;
    DISALLOW_COPY_AND_ASSIGN(ControllerReConfigData);
};

class ControllerDelPeerData : public ControllerWorkQueueData {
public:
    ControllerDelPeerData(AgentXmppChannel *ch);
    virtual ~ControllerDelPeerData() {}
    AgentXmppChannel *channel() {
        return channel_;
    }

private:
    AgentXmppChannel *channel_;
    DISALLOW_COPY_AND_ASSIGN(ControllerDelPeerData);
};

class VNController {
public:
    typedef boost::function<void(uint8_t)> XmppChannelDownCb;
    typedef boost::shared_ptr<ControllerXmppData> ControllerXmppDataType;
    typedef boost::shared_ptr<ControllerWorkQueueData> ControllerWorkQueueDataType;
    typedef boost::shared_ptr<ControllerReConfigData> ControllerReConfigDataType;
    typedef boost::shared_ptr<ControllerDelPeerData> ControllerDelPeerDataType;
    typedef std::list<PeerPtr> BgpPeerList;
    typedef BgpPeerList::const_iterator BgpPeerConstIterator;
    typedef std::list<PeerPtr>::iterator BgpPeerIterator;
    typedef boost::shared_ptr<AgentXmppChannel> AgentXmppChannelPtr;
    typedef std::vector<AgentXmppChannelPtr> AgentXmppChannelList;
    typedef AgentXmppChannelList::iterator AgentXmppChannelListIter;

    struct FabricMulticastLabelRange {
        FabricMulticastLabelRange() : start(), end(), fabric_multicast_label_range_str() {};
        ~FabricMulticastLabelRange() {};

        uint32_t start;
        uint32_t end;
        std::string fabric_multicast_label_range_str;
    };

    VNController(Agent *agent);
    virtual ~VNController();
    void Connect();
    void DisConnect();
    void ReConnect();
    void ReConnectXmppServer();
    void ReConnectDnsServer();

    void Cleanup();

    void XmppServerConnect();
    void DnsXmppServerConnect();

    void XmppServerDisConnect();
    void DnsXmppServerDisConnect();

    void DisConnectControllerIfmapServer(uint8_t idx);
    void DisConnectDnsServer(uint8_t idx);

    //Multicast peer identifier
    void increment_multicast_sequence_number() {multicast_sequence_number_++;}
    uint64_t multicast_sequence_number() {return multicast_sequence_number_;}

    //Peer maintenace routines 
    uint8_t ActiveXmppConnectionCount();
    AgentXmppChannel *GetActiveXmppChannel();

    AgentIfMapVmExport *agent_ifmap_vm_export() const {
        return agent_ifmap_vm_export_.get();
    }

    //Start/stop eor processors
    void StartEndOfRibTxTimer();
    void StopEndOfRibTx();

    bool ControllerWorkQueueProcess(ControllerWorkQueueDataType data);
    bool XmppMessageProcess(ControllerXmppDataType data);
    Agent *agent() {return agent_;}
    void Enqueue(ControllerWorkQueueDataType data);
    void DeleteAgentXmppChannel(uint8_t idx);
    void SetAgentMcastLabelRange(uint8_t idx);
    void FillMcastLabelRange(uint32_t *star_idx,
                             uint32_t *end_idx,
                             uint8_t idx) const;
    const FabricMulticastLabelRange &fabric_multicast_label_range(uint8_t idx) const {
        return fabric_multicast_label_range_[idx];
    }
    void RegisterControllerChangeCallback(XmppChannelDownCb xmpp_channel_down_cb) {
        xmpp_channel_down_cb_ = xmpp_channel_down_cb;
    }
    void FlushTimedOutChannels(uint8_t index);
    void DelPeerWalkDone(AgentXmppChannel *ch);
    void DelPeerWalkDoneProcess(AgentXmppChannel *ch);
    void StartDelPeerWalk(AgentXmppChannelPtr ch);
    bool RxXmppMessageTrace(uint8_t peer_index,
                            const std::string &to_address,
                            int port, int size,
                            const std::string &msg,
                            const XmppStanza::XmppMessage *xmpp_msg);
    bool TxXmppMessageTrace(uint8_t peer_index,
                            const std::string &to_address,
                            int port, int size,
                            const std::string &msg,
                            const XmppStanza::XmppMessage *xmpp_msg);
    //Unit test helpers
    bool IsWorkQueueEmpty() const;

private:
    void SetDscpConfig(XmppChannelConfig *xmpp_cfg) const;
    AgentXmppChannel *FindAgentXmppChannel(const std::string &server_ip);
    AgentDnsXmppChannel *FindAgentDnsXmppChannel(const std::string &server_ip);
    void DeleteConnectionInfo(const std::string &addr, bool is_dns) const;
    const std::string MakeConnectionPrefix(bool is_dns) const;
    bool AgentReConfigXmppServerConnectedExists(const std::string &server_ip,
                               std::vector<std::string> resp);
    bool ApplyControllerReConfigInternal(std::vector<string>service_list);
    bool ApplyDnsReConfigInternal(std::vector<string>service_list);

    Agent *agent_;
    uint64_t multicast_sequence_number_;
    boost::scoped_ptr<AgentIfMapVmExport> agent_ifmap_vm_export_;
    WorkQueue<ControllerWorkQueueDataType> work_queue_;
    FabricMulticastLabelRange fabric_multicast_label_range_[MAX_XMPP_SERVERS];
    XmppChannelDownCb xmpp_channel_down_cb_;
    uint32_t controller_list_chksum_;
    uint32_t dns_list_chksum_;
    AgentXmppChannelList timed_out_channels_[MAX_XMPP_SERVERS];
    AgentXmppChannelList delpeer_walks_;
    bool disconnect_;
};

extern SandeshTraceBufferPtr ControllerInfoTraceBuf;
extern SandeshTraceBufferPtr ControllerTxConfigTraceBuf1;
extern SandeshTraceBufferPtr ControllerTxConfigTraceBuf2;
extern SandeshTraceBufferPtr ControllerRouteWalkerTraceBuf;
extern SandeshTraceBufferPtr ControllerTraceBuf;
extern SandeshTraceBufferPtr ControllerRxRouteMessageTraceBuf1;
extern SandeshTraceBufferPtr ControllerRxConfigMessageTraceBuf1;
extern SandeshTraceBufferPtr ControllerRxRouteMessageTraceBuf2;
extern SandeshTraceBufferPtr ControllerRxConfigMessageTraceBuf2;
extern SandeshTraceBufferPtr ControllerTxMessageTraceBuf1;
extern SandeshTraceBufferPtr ControllerTxMessageTraceBuf2;

#define CONTROLLER_RX_ROUTE_MESSAGE_TRACE(obj, index, ...)\
do {\
    if (index == 0) { \
        AgentXmpp##obj::TraceMsg(ControllerRxRouteMessageTraceBuf1, __FILE__, \
                                 __LINE__, __VA_ARGS__);\
    } else { \
        AgentXmpp##obj::TraceMsg(ControllerRxRouteMessageTraceBuf2, __FILE__, \
                                 __LINE__, __VA_ARGS__);\
    } \
} while(0);\

#define CONTROLLER_RX_CONFIG_MESSAGE_TRACE(obj, index, ...)\
do {\
    if (index == 0) { \
        AgentXmpp##obj::TraceMsg(ControllerRxConfigMessageTraceBuf1, __FILE__, \
                                 __LINE__, __VA_ARGS__);\
    } else { \
        AgentXmpp##obj::TraceMsg(ControllerRxConfigMessageTraceBuf2, __FILE__, \
                                 __LINE__, __VA_ARGS__);\
    } \
} while(0);\

#define CONTROLLER_INFO_TRACE(obj, ...)\
do {\
    AgentXmpp##obj::TraceMsg(ControllerInfoTraceBuf, __FILE__, __LINE__, __VA_ARGS__);\
} while(0);\

#define CONTROLLER_TX_CONFIG_TRACE(obj, index, ...)\
do {\
    if (index == 0) { \
        AgentXmpp##obj::TraceMsg(ControllerTxConfigTraceBuf1, __FILE__, __LINE__, __VA_ARGS__);\
    } else { \
        AgentXmpp##obj::TraceMsg(ControllerTxConfigTraceBuf2, __FILE__, __LINE__, __VA_ARGS__);\
    } \
} while(0);\

#define CONTROLLER_ROUTE_WALKER_TRACE(obj, ...)\
do {\
    AgentXmpp##obj::TraceMsg(ControllerRouteWalkerTraceBuf, __FILE__, __LINE__, __VA_ARGS__);\
} while(0);\

#define CONTROLLER_CONNECTIONS_TRACE(obj, ...)\
do {\
    AgentXmpp##obj::TraceMsg(ControllerConnectionsTraceBuf, __FILE__, __LINE__, __VA_ARGS__);\
} while(0);\

#define CONTROLLER_TRACE(obj, ...)\
do {\
    AgentXmpp##obj::TraceMsg(ControllerTraceBuf, __FILE__, __LINE__, __VA_ARGS__);\
} while(0);\

#define CONTROLLER_TX_MESSAGE_TRACE(obj, index, ...)\
do {\
    if (index == 0) { \
        AgentXmpp##obj::TraceMsg(ControllerTxMessageTraceBuf1, __FILE__, \
                                 __LINE__, __VA_ARGS__);\
    } else { \
        AgentXmpp##obj::TraceMsg(ControllerTxMessageTraceBuf2, __FILE__, \
                                 __LINE__, __VA_ARGS__);\
    } \
} while(0);\

#endif
