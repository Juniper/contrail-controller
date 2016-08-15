/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __XMPP_CHANNEL_MUX_H__
#define __XMPP_CHANNEL_MUX_H__

#include <boost/system/error_code.hpp>
#include <tbb/atomic.h>
#include <tbb/mutex.h>
#include "xmpp/xmpp_channel.h"
#include "xmpp/xmpp_proto.h"
#include "xmpp/xmpp_state_machine.h"

class XmppConnection;

class XmppChannelMux : public XmppChannel {
public:
    explicit XmppChannelMux(XmppConnection *); 
    virtual ~XmppChannelMux();

    virtual void Close();
    virtual bool IsCloseInProgress() const;
    virtual void CloseComplete();
    virtual bool Send(const uint8_t *, size_t, xmps::PeerId, SendReadyCb);
    virtual int GetTaskInstance() const;
    virtual void RegisterReceive(xmps::PeerId, ReceiveCb);
    virtual void UnRegisterReceive(xmps::PeerId);
    virtual void RegisterRxMessageTraceCallback(RxMessageTraceCb cb);
    virtual void RegisterTxMessageTraceCallback(TxMessageTraceCb cb);
    size_t ReceiverCount() const;
    std::vector<std::string> GetReceiverList() const;

    virtual std::string ToString() const;
    virtual std::string StateName() const;
    virtual xmps::PeerState GetPeerState() const;
    virtual std::string FromString() const;

    virtual std::string LastStateName() const;
    virtual std::string LastStateChangeAt() const;
    virtual std::string LastEvent() const;
    virtual uint32_t rx_open() const;
    virtual uint32_t rx_close() const;
    virtual uint32_t rx_update() const;
    virtual uint32_t rx_keepalive() const;
    virtual uint32_t tx_open() const;
    virtual uint32_t tx_close() const;
    virtual uint32_t tx_update() const;
    virtual uint32_t tx_keepalive() const;
    virtual uint32_t FlapCount() const;
    virtual std::string LastFlap() const;
    virtual std::string AuthType() const;
    virtual std::string PeerAddress() const;
    virtual bool LastReceived(uint64_t durationMsec) const;
    virtual bool LastSent(uint64_t durationMsec) const;

    virtual void ProcessXmppMessage(const XmppStanza::XmppMessage *msg);
    void WriteReady(const boost::system::error_code &ec);
    virtual void UnRegisterWriteReady(xmps::PeerId id);

    void HandleStateEvent(xmsm::XmState state);

    virtual const XmppConnection *connection() const { return connection_; }
    virtual XmppConnection *connection() { return connection_; }
    bool RxMessageTrace(const std::string &to_address, int port, int msg_size,
                        const std::string &msg,
                        const XmppStanza::XmppMessage *xmpp_msg);

    bool TxMessageTrace(const std::string &to_address, int port, int msg_size,
                        const std::string &msg,
                        const XmppStanza::XmppMessage *xmpp_msg);

protected:
    friend class XmppChannelMuxMock;

private:
    void RegisterWriteReady(xmps::PeerId, SendReadyCb);
    bool InitializeClosingCount();

    typedef std::map<xmps::PeerId, SendReadyCb> WriteReadyCbMap;
    typedef std::map<xmps::PeerId, ReceiveCb> ReceiveCbMap;

    WriteReadyCbMap map_;
    ReceiveCbMap rxmap_;
    SendReadyCb cb_;
    XmppConnection *connection_;
    tbb::mutex mutex_;
    RxMessageTraceCb rx_message_trace_cb_;
    TxMessageTraceCb tx_message_trace_cb_;
    tbb::atomic<int> closing_count_;
    tbb::atomic<uint64_t> last_received_;
    tbb::atomic<uint64_t> last_sent_;
};

#endif // __XMPP_CHANNEL_MUX_H__
