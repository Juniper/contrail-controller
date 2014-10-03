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

class AgentIfMapXmppChannel {
public:
    explicit AgentIfMapXmppChannel(Agent *agent, XmppChannel *channel,
                                   uint8_t count);
    virtual ~AgentIfMapXmppChannel();

    virtual const std::string &identifier() const {
        return identifier_;
    }

    virtual std::string ToString() const;
    virtual bool SendUpdate(const std::string &msg);
    virtual void ReceiveUpdate(const XmppStanza::XmppMessage *msg);
    uint8_t GetXmppServerIdx() { return xs_idx_; }
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
    Agent *agent_;
};

class AgentIfMapVmExport {
public:
    struct VmExportInfo {
        std::list<boost::uuids::uuid> vmi_list_;
        uint64_t seq_number_;
    };

    AgentIfMapVmExport(Agent *agent);
    ~AgentIfMapVmExport();
    void Notify(DBTablePartBase *partition, DBEntryBase *e);
    void NotifyAll(AgentXmppChannel *peer);
    typedef std::map<boost::uuids::uuid, struct VmExportInfo *> VmMap; 
    Agent *agent() const {return agent_;}

private:
    DBTableBase::ListenerId vmi_list_id_;
    VmMap vm_map_;
    Agent *agent_;
};
#endif // __IFMAP_XMPP_H__
