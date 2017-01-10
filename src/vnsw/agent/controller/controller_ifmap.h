/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __IFMAP_XMPP_H__
#define __IFMAP_XMPP_H__

#include <map>
#include <string>

#include <boost/function.hpp>
#include <boost/system/error_code.hpp>

#include <xmpp/xmpp_channel.h>
#include <cmn/agent_cmn.h>

class XmppChannel;
class AgentXmppChannel;
class ControllerVmiSubscribeData;

class AgentIfMapXmppChannel {
public:
    struct EndOfConfigParams {
        void Reset();
        uint64_t update_receive_time_;
        uint64_t config_enqueued_time_;
    };
    explicit AgentIfMapXmppChannel(Agent *agent, XmppChannel *channel,
                                   uint8_t count);
    virtual ~AgentIfMapXmppChannel();

    virtual const std::string &identifier() const {
        return identifier_;
    }

    virtual std::string ToString() const;
    virtual bool SendUpdate(const std::string &msg);
    void ReceiveConfigMessage(std::auto_ptr<XmlBase> impl);
    virtual void ReceiveUpdate(const XmppStanza::XmppMessage *msg);
    uint8_t GetXmppServerIdx() { return xs_idx_; }
    EndOfConfigParams &end_of_config_params() {return end_of_config_params_;}
    static uint64_t GetSeqNumber() { return seq_number_; }
    static uint64_t NewSeqNumber(); 

protected:
    virtual void WriteReadyCb(const boost::system::error_code &ec);

private:
    void ReceiveInternal(const XmppStanza::XmppMessage *msg);
    XmppChannel *channel_;
    std::string identifier_;
    uint8_t xs_idx_;
    static uint64_t seq_number_;
    EndOfConfigParams end_of_config_params_;
    Agent *agent_;
};

class AgentIfMapVmExport {
public:
    typedef std::list<boost::uuids::uuid> UuidList;
    struct VmExportInfo {
        UuidList vmi_list_;
        uint64_t seq_number_;

        VmExportInfo(uint64_t seq_no) : vmi_list_(), seq_number_(seq_no) { }
        ~VmExportInfo() { }
    };

    AgentIfMapVmExport(Agent *agent);
    ~AgentIfMapVmExport();

    void VmiAdd(const ControllerVmiSubscribeData *entry);
    void VmiDelete(const ControllerVmiSubscribeData *entry);
    void VmiEvent(const ControllerVmiSubscribeData *entry);
    void NotifyAll(AgentXmppChannel *peer);
    typedef std::map<boost::uuids::uuid, struct VmExportInfo *> VmMap; 
    Agent *agent() const {return agent_;}

private:
    DBTableBase::ListenerId vmi_list_id_;
    VmMap vm_map_;
    Agent *agent_;
};
#endif // __IFMAP_XMPP_H__
