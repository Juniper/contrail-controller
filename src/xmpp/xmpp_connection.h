/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __XMPP_CHANNEL_H__
#define __XMPP_CHANNEL_H__

#include <boost/asio/ip/tcp.hpp>
#include <tbb/spin_mutex.h>

#include "base/timer.h"
#include "base/lifetime.h"
#include "net/address.h"
#include "xmpp/xmpp_channel_mux.h"
#include "xmpp/xmpp_state_machine.h"
#include "xmpp/xmpp_session.h"

class LifetimeActor;
class TcpServer;
class TcpSession;
class XmppChannelConfig;
class XmppConnectionEndpoint;
class XmppServer;
class XmppSession;

class XmppConnection {
public:
    static const int keepAliveTime = 30;
    static const int GetDefaultkeepAliveTime();

    struct ProtoStats {
        ProtoStats() : open(0), close(0), keepalive(0), update(0) {
        }
        uint32_t open;
        uint32_t close;
        uint32_t keepalive;
        uint32_t update;
    };

    XmppConnection(TcpServer *server, const XmppChannelConfig *config);
    virtual ~XmppConnection();

    void SetConfig(const XmppChannelConfig *);

    // Invoked from XmppServer when a session is accepted.
    virtual bool AcceptSession(XmppSession *session);
    virtual void ReceiveMsg(XmppSession *session, const std::string &); 

    virtual boost::asio::ip::tcp::endpoint endpoint() const;
    virtual boost::asio::ip::tcp::endpoint local_endpoint() const;
    TcpServer *server() { return server_; }
    XmppSession *CreateSession();

    std::string ToString() const; 
    std::string ToUVEKey() const; 
    std::string FromString() const;
    void SetAdminDown(bool toggle);
    bool Send(const uint8_t *data, size_t size);

    // Xmpp connection messages
    virtual void SendOpen(TcpSession *session);
    virtual void SendOpenConfirm(TcpSession *session);
    void SendKeepAlive();
    void SendClose(TcpSession *session);

    // PubSub Messages
    int ProcessXmppIqMessage(const XmppStanza::XmppMessage *);

    // chat messages
    int ProcessXmppChatMessage(const XmppStanza::XmppChatMessage *);

    void SetKeepAliveTimer(int seconds) {
        keepalive_time_ = seconds; 
        StopKeepAliveTimer();
        StartKeepAliveTimer();
    }
    int  GetKeepAliveTimer() { return keepalive_time_; }

    void StartKeepAliveTimer();
    void StopKeepAliveTimer();

    void set_session(XmppSession *session);
    void SetFrom(const std::string &);
    void SetTo(const std::string &);

    const XmppSession *session() const;
    XmppSession *session();

    bool logUVE() const {
        return ((IsClient() == false) && (log_uve_));
    }
    virtual bool IsClient() const = 0;
    virtual void ManagedDelete() = 0;
    virtual void RetryDelete() = 0;
    virtual LifetimeActor *deleter() = 0;
    virtual const LifetimeActor *deleter() const = 0;
    virtual LifetimeManager *lifetime_manager() = 0;
    virtual void Destroy() = 0;
    xmsm::XmState GetStateMcState() const;

    std::string StateName() const { return state_machine_->StateName(); }
    bool IsActiveChannel() const {
        return state_machine_->IsActiveChannel();
    }
    XmppChannelMux *ChannelMux() {return mux_.get(); }
    void SetChannelMux(XmppChannelMux *channel_mux) { mux_.reset(channel_mux); }

    int GetIndex() const {
        //TODO: implement this
        return 0;
    }

    void Initialize() { state_machine_->Initialize(); }
    void Clear() { state_machine_->Clear(); }
    void SetAdminState(bool down) { state_machine_->SetAdminState(down); }

    void Shutdown();
    bool ShutdownPending() const;
    bool MayDelete() const;

    std::string &GetComputeHostName() { return to_; }
    std::string &GetControllerHostName() { return from_; }

    virtual void set_close_reason(const std::string &reason) = 0;
    virtual void increment_flap_count() = 0;
    virtual uint32_t flap_count() const = 0;
    virtual const std::string last_flap_at() const = 0;

    virtual void WriteReady(const boost::system::error_code &ec);

    friend class XmppStateMachineTest;

    const std::string LastStateName() const {
        return state_machine_->LastStateName();
    }

    const std::string LastStateChangeAt() const {
        return state_machine_->LastStateChangeAt();
    }

    const std::string LastEvent() const {
        return state_machine_->last_event();
    }

    uint32_t tx_open() const {
        return stats_[1].open;
    }
    uint32_t tx_keepalive() const {
        return stats_[1].keepalive;
    }
    uint32_t tx_close() const {
        return stats_[1].close;
    }
    uint32_t tx_update() const {
        return stats_[1].update;
    }


    uint32_t rx_open() const {
        return stats_[0].open;
    }
    uint32_t rx_keepalive() const {
        return stats_[0].keepalive;
    }
    uint32_t rx_close() const {
        return stats_[0].close;
    }
    uint32_t rx_update() const {
        return stats_[0].update;
    }

    void LogMsg(std::string msg);
    bool disable_read() const { return disable_read_; }
    void set_disable_read(bool disable_read) { disable_read_ = disable_read; }
    XmppStateMachine *state_machine();
protected:
    const XmppStateMachine *state_machine() const;
    bool is_deleted_;

private:
    bool KeepAliveTimerExpired();
    void KeepaliveTimerErrorHanlder(std::string error_name,
                                    std::string error_message);
    XmppStanza::XmppMessage *XmppDecode(const std::string &msg);
    void LogKeepAliveSend();

    TcpServer *server_;
    boost::asio::ip::tcp::endpoint endpoint_;
    boost::asio::ip::tcp::endpoint local_endpoint_;
    const XmppChannelConfig *config_;
    // Protection for session_ and keepalive_timer_
    tbb::spin_mutex spin_mutex_;
    XmppSession *session_;
    std::auto_ptr<XmppStateMachine> state_machine_;
    Timer *keepalive_timer_;

    bool log_uve_;

    bool admin_down_;
    std::string from_; // bare jid
    std::string to_;
    std::auto_ptr<XmppChannelMux> mux_; 
    int keepalive_time_;
    std::auto_ptr<XmppStanza::XmppMessage> last_msg_;

    ProtoStats stats_[2];
    bool     disable_read_;

    void IncProtoStats(unsigned int type);
    DISALLOW_COPY_AND_ASSIGN(XmppConnection);
};

class XmppServerConnection : public XmppConnection {
public:
    XmppServerConnection(XmppServer *server, const XmppChannelConfig *config);
    virtual ~XmppServerConnection();
    virtual bool IsClient() const;
    virtual void ManagedDelete();
    virtual void RetryDelete();
    virtual LifetimeActor *deleter();
    virtual const LifetimeActor *deleter() const;
    virtual LifetimeManager *lifetime_manager();
    virtual void Destroy();

    virtual void set_close_reason(const std::string &reason);
    virtual uint32_t flap_count() const;
    virtual void increment_flap_count();
    virtual const std::string last_flap_at() const;

private:
    class DeleteActor;
    boost::scoped_ptr<DeleteActor> deleter_;
    LifetimeRef<XmppServerConnection> server_delete_ref_;
    XmppConnectionEndpoint *conn_endpoint_;
};

class XmppClientConnection : public XmppConnection {
public:
    XmppClientConnection(TcpServer *server, const XmppChannelConfig *config);
    virtual ~XmppClientConnection();
    virtual bool IsClient() const;
    virtual void ManagedDelete();
    virtual void RetryDelete();
    virtual LifetimeActor *deleter();
    virtual const LifetimeActor *deleter() const;
    virtual LifetimeManager *lifetime_manager();
    virtual void Destroy();

    virtual void set_close_reason(const std::string &reason);
    virtual uint32_t flap_count() const;
    virtual void increment_flap_count();
    virtual const std::string last_flap_at() const;

private:
    class DeleteActor;
    boost::scoped_ptr<DeleteActor> deleter_;
    LifetimeRef<XmppClientConnection> server_delete_ref_;
    std::string close_reason_;
    uint32_t flap_count_;
    uint64_t last_flap_;
};

class XmppConnectionEndpoint {
public:
    XmppConnectionEndpoint(Ip4Address address);

    void set_close_reason(const std::string &close_reason);
    uint32_t flap_count() const;
    void increment_flap_count();
    uint64_t last_flap() const;
    const std::string last_flap_at() const;

private:
    Ip4Address address_;
    std::string close_reason_;
    uint32_t flap_count_;
    uint64_t last_flap_;
};

#endif // __XMPP_CHANNEL_H__
