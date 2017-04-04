/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "xmpp/xmpp_connection.h"

#include <boost/date_time/posix_time/posix_time.hpp>
#include <sstream>

#include "base/lifetime.h"
#include "base/task_annotations.h"
#include "io/event_manager.h"
#include "xml/xml_base.h"
#include "xmpp/xmpp_client.h"
#include "xmpp/xmpp_config.h"
#include "xmpp/xmpp_factory.h"
#include "xmpp/xmpp_log.h"
#include "xmpp/xmpp_server.h"
#include "xmpp/xmpp_session.h"

#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_trace.h"
#include "sandesh/common/vns_types.h"
#include "sandesh/common/vns_constants.h"
#include "sandesh/xmpp_client_server_sandesh_types.h"
#include "sandesh/xmpp_message_sandesh_types.h"
#include "sandesh/xmpp_server_types.h"
#include "sandesh/xmpp_state_machine_sandesh_types.h"
#include "sandesh/xmpp_trace_sandesh_types.h"
#include "sandesh/xmpp_peer_info_types.h"

using namespace std;
using boost::system::error_code;

const char *XmppConnection::kAuthTypeNil = "NIL";
const char *XmppConnection::kAuthTypeTls = "TLS";

XmppConnection::XmppConnection(TcpServer *server, 
                               const XmppChannelConfig *config)
    : server_(server),
      session_(NULL),
      endpoint_(config->endpoint),
      local_endpoint_(config->local_endpoint),
      config_(NULL),
      keepalive_timer_(TimerManager::CreateTimer(
                  *server->event_manager()->io_service(),
                  "Xmpp keepalive timer",
                  TaskScheduler::GetInstance()->GetTaskId("xmpp::StateMachine"),
                  GetTaskInstance(config->ClientOnly()))),
      is_client_(config->ClientOnly()),
      log_uve_(config->logUVE),
      admin_down_(false), 
      disable_read_(false),
      from_(config->FromAddr),
      to_(config->ToAddr),
      auth_enabled_(config->auth_enabled),
      dscp_value_(config->dscp_value),
      state_machine_(XmppObjectFactory::Create<XmppStateMachine>(
          this, config->ClientOnly(), config->auth_enabled)),
      mux_(XmppObjectFactory::Create<XmppChannelMux>(this)) {
    ostringstream oss;
    oss << FromString() << ":" << endpoint().address().to_string();
    uve_key_str_ = oss.str();
}

XmppConnection::~XmppConnection() {
    StopKeepAliveTimer();
    TimerManager::DeleteTimer(keepalive_timer_);
    XMPP_UTDEBUG(XmppConnectionDelete, "XmppConnection destructor",
               FromString(), ToString());
}

std::string XmppConnection::GetXmppAuthenticationType() const {
    if (auth_enabled_) {
        return (XmppConnection::kAuthTypeTls);
    } else {
        return (XmppConnection::kAuthTypeNil);
    }
}

void XmppConnection::SetConfig(const XmppChannelConfig *config) {
    config_ = config;
}

void XmppConnection::set_session(XmppSession *session) {
    tbb::spin_mutex::scoped_lock lock(spin_mutex_);
    assert(session);
    session_ = session;
    if (session_ && dscp_value_) {
        session_->SetDscpSocketOption(dscp_value_);
    }
}

void XmppConnection::clear_session() {
    tbb::spin_mutex::scoped_lock lock(spin_mutex_);
    if (!session_)
        return;
    session_->ClearConnection();
    session_ = NULL;
}

const XmppSession *XmppConnection::session() const {
    return session_;
}

XmppSession *XmppConnection::session() {
    return session_;
}

void XmppConnection::WriteReady() {
    boost::system::error_code ec;
    mux_->WriteReady(ec);
}

void XmppConnection::Shutdown() {
    ManagedDelete();
}

bool XmppConnection::IsDeleted() const {
    return deleter()->IsDeleted();
}

bool XmppConnection::MayDelete() const {
    return (mux_->ReceiverCount() == 0);
}

XmppSession *XmppConnection::CreateSession() {
    TcpSession *session = server_->CreateSession();
    XmppSession *xmpp_session = static_cast<XmppSession *>(session);
    xmpp_session->SetConnection(this);
    return xmpp_session;
}

//
// Return the task instance for this XmppConnection.
// Calculate from the remote IpAddress so that a restarting session uses the
// same value as before.
// Do not make this method virtual since it gets called from the constructor.
//

int XmppConnection::GetTaskInstance(bool is_client) const {
    if (is_client)
        return 0;
    IpAddress address = endpoint().address();
    int thread_count = TaskScheduler::GetInstance()->HardwareThreadCount();
    if (address.is_v4()) {
        return address.to_v4().to_ulong() % thread_count;
    } else {
        return 0;
    }
}

xmsm::XmState XmppConnection::GetStateMcState() const {
    const XmppStateMachine *sm = state_machine();
    assert(sm);
    return sm->StateType();
}

xmsm::XmOpenConfirmState XmppConnection::GetStateMcOpenConfirmState() const {
    const XmppStateMachine *sm = state_machine();
    assert(sm);
    return sm->OpenConfirmStateType();
}


boost::asio::ip::tcp::endpoint XmppConnection::endpoint() const {
    return endpoint_;
}

boost::asio::ip::tcp::endpoint XmppConnection::local_endpoint() const {
    return local_endpoint_;
}

string XmppConnection::endpoint_string() const {
    ostringstream oss;
    oss << endpoint_;
    return oss.str();
}

string XmppConnection::local_endpoint_string() const {
    ostringstream oss;
    oss << local_endpoint_;
    return oss.str();
}

const string &XmppConnection::FromString() const {
    return from_;
}

const string &XmppConnection::ToString() const {
    return to_;
}

const std::string &XmppConnection::ToUVEKey() const {
    return uve_key_str_;
}

static void XMPPPeerInfoSend(XmppPeerInfoData &peer_info) {
    XMPPPeerInfo::Send(peer_info);
}

void XmppConnection::SetTo(const string &to) {
    if ((to_.size() == 0) && (to.size() != 0)) {
        to_ = to;
        if (!logUVE()) return;
        XmppPeerInfoData peer_info;
        peer_info.set_name(ToUVEKey());
        peer_info.set_identifier(to_);
        XMPPPeerInfoSend(peer_info);
    }
}

void XmppConnection::SetAdminDown(bool toggle) {
    // TODO: generate state machine event.
    admin_down_ = toggle;
}

bool XmppConnection::AcceptSession(XmppSession *session) {
    session->SetConnection(this);
    return state_machine_->PassiveOpen(session);
}

bool XmppConnection::Send(const uint8_t *data, size_t size,
    const string *msg_str) {
    tbb::spin_mutex::scoped_lock lock(spin_mutex_);
    if (session_ == NULL) {
        return false;
    }

    TcpSession::Endpoint endpoint = session_->remote_endpoint();
    const string &endpoint_addr_str = session_->remote_addr_string();
    string str;
    if (!msg_str) {
        str.append(reinterpret_cast<const char *>(data), size);
        msg_str = &str;
    }

    if (!(mux_ &&
         (mux_->TxMessageTrace(endpoint_addr_str, endpoint.port(),
                               size, *msg_str, NULL)))) {
        XMPP_MESSAGE_TRACE(XmppTxStream,
                           endpoint_addr_str, endpoint.port(), size, *msg_str);
    }

    stats_[1].update++;
    size_t sent;
    return session_->Send(data, size, &sent);
}

int XmppConnection::SetDscpValue(uint8_t value) {
    tbb::spin_mutex::scoped_lock lock(spin_mutex_);
    dscp_value_ = value;
    if (!session_) {
        return 0;
    }
    return session_->SetDscpSocketOption(value);
}

bool XmppConnection::SendOpen(XmppSession *session) {
    if (!session) return false;
    XmppProto::XmppStanza::XmppStreamMessage openstream;
    openstream.strmtype = XmppStanza::XmppStreamMessage::INIT_STREAM_HEADER;
    uint8_t data[256];
    int len = XmppProto::EncodeStream(openstream, to_, from_, data, 
                                      sizeof(data));
    if (len <= 0) {
        inc_open_fail();
        return false;
    } else {
        XMPP_UTDEBUG(XmppOpen, len, from_, to_);
        session->Send(data, len, NULL);
        stats_[1].open++;
        return true;
    }
}

bool XmppConnection::SendOpenConfirm(XmppSession *session) {
    tbb::spin_mutex::scoped_lock lock(spin_mutex_);
    if (!session_) return false;
    XmppStanza::XmppStreamMessage openstream;
    openstream.strmtype = XmppStanza::XmppStreamMessage::INIT_STREAM_HEADER_RESP;
    uint8_t data[256];
    int len = XmppProto::EncodeStream(openstream, to_, from_, data, 
                                      sizeof(data));
    if (len <= 0) {
        inc_open_fail();
        return false;
    } else {
        XMPP_UTDEBUG(XmppOpenConfirm, len, from_, to_);
        session_->Send(data, len, NULL);
        stats_[1].open++;
        return true;
    }
}

bool XmppConnection::SendStreamFeatureRequest(XmppSession *session) {
    tbb::spin_mutex::scoped_lock lock(spin_mutex_);
    if (!session_) return false;
    XmppStanza::XmppStreamMessage featurestream;
    featurestream.strmtype = XmppStanza::XmppStreamMessage::FEATURE_TLS;
    featurestream.strmtlstype = XmppStanza::XmppStreamMessage::TLS_FEATURE_REQUEST;
    uint8_t data[256];
    int len = XmppProto::EncodeStream(featurestream, to_, from_, data,
                                      sizeof(data));
    if (len <= 0) {
        inc_stream_feature_fail();
        return false;
    } else {
        XMPP_UTDEBUG(XmppControlMessage, "Send Stream Feature Request",
                     len, from_, to_);
        session_->Send(data, len, NULL);
        //stats_[1].open++;
        return true;
    }
}

bool XmppConnection::SendStartTls(XmppSession *session) {
    tbb::spin_mutex::scoped_lock lock(spin_mutex_);
    if (!session_) return false;
    XmppStanza::XmppStreamMessage stream;
    stream.strmtype = XmppStanza::XmppStreamMessage::FEATURE_TLS;
    stream.strmtlstype = XmppStanza::XmppStreamMessage::TLS_START;
    uint8_t data[256];
    int len = XmppProto::EncodeStream(stream, to_, from_, data,
                                      sizeof(data));
    if (len <= 0) {
        inc_stream_feature_fail();
        return false;
    } else {
        XMPP_UTDEBUG(XmppControlMessage, "Send Start Tls", len, from_, to_);
        session_->Send(data, len, NULL);
        //stats_[1].open++;
        return true;
    }
}

bool XmppConnection::SendProceedTls(XmppSession *session) {
    tbb::spin_mutex::scoped_lock lock(spin_mutex_);
    if (!session_) return false;
    XmppStanza::XmppStreamMessage stream;
    stream.strmtype = XmppStanza::XmppStreamMessage::FEATURE_TLS;
    stream.strmtlstype = XmppStanza::XmppStreamMessage::TLS_PROCEED;
    uint8_t data[256];
    int len = XmppProto::EncodeStream(stream, to_, from_, data,
                                      sizeof(data));
    if (len <= 0) {
        inc_stream_feature_fail();
        return false;
    } else {
        XMPP_UTDEBUG(XmppControlMessage, "Send Proceed Tls", len, from_, to_);
        session_->Send(data, len, NULL);
        //stats_[1].open++;
        return true;
    }
}

void XmppConnection::SendClose(XmppSession *session) {
    tbb::spin_mutex::scoped_lock lock(spin_mutex_);
    if (!session_) return;
    string str("</stream:stream>");
    uint8_t data[64];
    memcpy(data, str.data(), str.size());
    XMPP_UTDEBUG(XmppClose, str.size(), from_, to_);
    session_->Send(data, str.size(), NULL);
    stats_[1].close++;
}

void XmppConnection::ProcessSslHandShakeResponse(SslSessionPtr session,
    const boost::system::error_code& error) {
    if (!state_machine())
        return;

    if (error) {
        inc_handshake_failure();
        state_machine()->OnEvent(session.get(), xmsm::EvTLSHANDSHAKE_FAILURE);

        if (error.category() == boost::asio::error::get_ssl_category()) {
            string err = error.message();
            err = string(" (")
                         +boost::lexical_cast<string>(ERR_GET_LIB(error.value()))+","
                         +boost::lexical_cast<string>(ERR_GET_FUNC(error.value()))+","
                         +boost::lexical_cast<string>(ERR_GET_REASON(error.value()))+") ";

             char buf[128];
             ::ERR_error_string_n(error.value(), buf, sizeof(buf));
             err += buf;
             XMPP_ALERT(XmppSslHandShakeFailure, "failure", err);
        }

    } else {
        XMPP_DEBUG(XmppSslHandShakeMessage, "success", "");
        state_machine()->OnEvent(session.get(), xmsm::EvTLSHANDSHAKE_SUCCESS);
    }
}

void XmppConnection::LogMsg(std::string msg) {
    log4cplus::Logger logger = log4cplus::Logger::getRoot();
    LOG4CPLUS_DEBUG(logger, msg << ToString() << " " <<
         local_endpoint_.address() << ":" << local_endpoint_.port() << "::" <<
         endpoint_.address() << ":" << endpoint_.port());
}

void XmppConnection::LogKeepAliveSend() {
    static bool init_ = false;
    static bool log_ = false;

    if (!init_) {
        char *str = getenv("XMPP_ASSERT_ON_HOLD_TIMEOUT");
        if (str && strtoul(str, NULL, 0) != 0) log_ = true;
        init_ = true;
    }

    if (log_) LogMsg("SEND KEEPALIVE: ");
}

void XmppConnection::SendKeepAlive() {
    tbb::spin_mutex::scoped_lock lock(spin_mutex_);
    if (!session_) return;
    XmppStanza::XmppMessage msg(XmppStanza::WHITESPACE_MESSAGE_STANZA);
    uint8_t data[256];
    int len = XmppProto::EncodeStream(msg, data, sizeof(data));
    assert(len > 0);
    session_->Send(data, len, NULL);
    stats_[1].keepalive++;
    LogKeepAliveSend();
}

bool XmppConnection::KeepAliveTimerExpired() {
    if (state_machine_->get_state() != xmsm::ESTABLISHED)
        return false;

    // TODO: check timestamp of last received packet.
    SendKeepAlive();

    //
    // Start the timer again, by returning true
    //
    return true;
}   

void XmppConnection::KeepaliveTimerErrorHanlder(string error_name,
                                                string error_message) {
    XMPP_WARNING(XmppKeepaliveTimeError, error_name, error_message);
}

void XmppConnection::StartKeepAliveTimer() {
    tbb::spin_mutex::scoped_lock lock(spin_mutex_);
    if (!session_)
        return;

    int holdtime_msecs = state_machine_->hold_time_msecs();
    if (holdtime_msecs <= 0)
        return;

    keepalive_timer_->Start(holdtime_msecs / 3,
        boost::bind(&XmppConnection::KeepAliveTimerExpired, this),
        boost::bind(&XmppConnection::KeepaliveTimerErrorHanlder, this, _1, _2));
}

void XmppConnection::StopKeepAliveTimer() {
    tbb::spin_mutex::scoped_lock lock(spin_mutex_);
    keepalive_timer_->Cancel();
}

XmppStateMachine *XmppConnection::state_machine() {
    return state_machine_.get();
}

const XmppStateMachine *XmppConnection::state_machine() const {
    return state_machine_.get();
}

const XmppChannelMux *XmppConnection::channel_mux() const {
    return mux_.get();
}

void XmppConnection::IncProtoStats(unsigned int type) {
    switch (type) {
        case XmppStanza::STREAM_HEADER:
            stats_[0].open++;
            break;
        case XmppStanza::WHITESPACE_MESSAGE_STANZA:
            stats_[0].keepalive++;
            break;
        case XmppStanza::IQ_STANZA:
            stats_[0].update++;
            break;
        case XmppStanza::MESSAGE_STANZA:
            stats_[0].update++;
            break;
    }
}

void XmppConnection::inc_connect_error() {
    error_stats_.connect_error++;
}

void XmppConnection::inc_session_close() {
    error_stats_.session_close++;
}

void XmppConnection::inc_open_fail() {
    error_stats_.open_fail++;
}

void XmppConnection::inc_stream_feature_fail() {
    error_stats_.stream_feature_fail++;
}

void XmppConnection::inc_handshake_failure() {
    error_stats_.handshake_fail++;
}

size_t XmppConnection::get_connect_error() {
    return error_stats_.connect_error;
}

size_t XmppConnection::get_session_close() {
    return error_stats_.session_close;
}

size_t XmppConnection::get_open_fail() {
    return error_stats_.open_fail;
}

size_t XmppConnection::get_stream_feature_fail() {
    return error_stats_.stream_feature_fail;
}

size_t XmppConnection::get_handshake_failure() {
    return error_stats_.handshake_fail;
}

size_t XmppConnection::get_sm_connect_attempts() {
    return state_machine_->get_connect_attempts();
}

size_t XmppConnection::get_sm_keepalive_count() {
    return state_machine_->get_keepalive_count();
}

void XmppConnection::ReceiveMsg(XmppSession *session, const string &msg) {
    XmppStanza::XmppMessage *minfo = XmppDecode(msg);

    if (minfo) {
        session->IncStats((unsigned int)minfo->type, msg.size());
        if (minfo->type != XmppStanza::WHITESPACE_MESSAGE_STANZA) {
            if (!(mux_ &&
                  (mux_->RxMessageTrace(session->
                                        remote_endpoint().address().to_string(),
                                        session->remote_endpoint().port(),
                                        msg.size(), msg, minfo)))) {
                XMPP_MESSAGE_TRACE(XmppRxStream,
                                   session->
                                   remote_endpoint().address().to_string(),
                                   session->
                                   remote_endpoint().port(), msg.size(), msg);
            }
        }
        IncProtoStats((unsigned int)minfo->type);
        state_machine_->OnMessage(session, minfo);
    } else if ((minfo = last_msg_.get()) != NULL) {
        session->IncStats((unsigned int)minfo->type, msg.size());
        IncProtoStats((unsigned int)minfo->type);
    } else {
        session->IncStats(XmppStanza::INVALID, msg.size());
        XMPP_MESSAGE_TRACE(XmppRxStreamInvalid,
             session->remote_endpoint().address().to_string(),
             session->remote_endpoint().port(), msg.size(), msg);
    }
    return;
}

XmppStanza::XmppMessage *XmppConnection::XmppDecode(const string &msg) {
    auto_ptr<XmppStanza::XmppMessage> minfo(XmppProto::Decode(msg));
    if (minfo.get() == NULL) {
        XMPP_INFO(XmppSessionDelete, "Server", FromString(), ToString());
        Clear();
        return NULL;
    }

    if (minfo->type == XmppStanza::IQ_STANZA) {
        const XmppStanza::XmppMessageIq *iq = 
            static_cast<const XmppStanza::XmppMessageIq *>(minfo.get());


        if (iq->action.compare("publish") == 0) {
            last_msg_.reset(minfo.release());
            return NULL;
        }

        if (iq->action.compare("collection") == 0) {
            if (last_msg_.get() != NULL) {
                XmppStanza::XmppMessageIq *last_iq = 
                    static_cast<XmppStanza::XmppMessageIq *>(last_msg_.get());

                if (last_iq->node.compare(iq->as_node) == 0) {
                    XmlBase *impl = last_iq->dom.get();
                    impl->ReadNode("publish");
                    impl->ModifyAttribute("node", iq->node);
                    last_iq->node = impl->ReadAttrib("node");
                    last_iq->is_as_node = iq->is_as_node;
                    //Save the complete ass/dissociate node
                    last_iq->as_node = iq->as_node;
                } else {
                    XMPP_WARNING(XmppIqMessageInvalid);
                    goto error;
                }
            } else {
                XMPP_LOG(XmppIqCollectionError);
                goto error;
            }
            // iq message merged with collection info
            return last_msg_.release();
        }
    }
    return minfo.release();

error:
    last_msg_.reset();
    return NULL;
}

int XmppConnection::ProcessXmppChatMessage(
        const XmppStanza::XmppChatMessage *msg) {
    mux_->ProcessXmppMessage(msg);
    return 0;
}

int XmppConnection::ProcessXmppIqMessage(const XmppStanza::XmppMessage *msg) {
    mux_->ProcessXmppMessage(msg);
    return 0;
}

class XmppServerConnection::DeleteActor : public LifetimeActor {
public:
    DeleteActor(XmppServer *server, XmppServerConnection *parent)
        : LifetimeActor(server->lifetime_manager()),
          server_(server), parent_(parent) {
    }

    virtual bool MayDelete() const {
        return (!parent_->on_work_queue() && parent_->MayDelete());
    }

    virtual void Shutdown() {
        CHECK_CONCURRENCY("bgp::Config");

        // If the connection is still on the WorkQueue, simply add it to the
        // ConnectionSet.  It won't be on the ConnectionMap.
        if (parent_->on_work_queue()) {
            server_->InsertDeletedConnection(parent_);
        }

        // If the connection was rejected as duplicate, it will already be in
        // the ConnectionSet. Non-duplicate connections need to be moved from
        // from the ConnectionMap into the ConnectionSet.  We add it to the
        // ConnectionSet and then remove it from ConnectionMap to ensure that
        // the XmppServer connection count doesn't temporarily become 0. This
        // is friendly to tests that wait for the XmppServer connection count
        // to become 0.
        //
        // Breaking association with the XmppConnectionEndpoint here allows
        // a new XmppServerConnection with the same Endpoint to come up. We
        // may end up leaking memory if current XmppServerConnection doesn't
        // get cleaned up completely, but we at least prevent the other end
        // from getting stuck forever.
        else if (!parent_->duplicate()) {
            server_->InsertDeletedConnection(parent_);
            server_->RemoveConnection(parent_);
            server_->ReleaseConnectionEndpoint(parent_);
        }

        if (parent_->session() || server_->IsPeerCloseGraceful()) {
            parent_->ChannelMux()->HandleStateEvent(xmsm::IDLE);
        }

        if (parent_->logUVE()) {
            XmppPeerInfoData peer_info;
            peer_info.set_name(parent_->ToUVEKey());
            peer_info.set_deleted(true);
            XMPPPeerInfoSend(peer_info);
        }

        XmppSession *session = NULL;
        if (parent_->state_machine()) {
            session = parent_->state_machine()->session();
            parent_->state_machine()->clear_session();
        }
        if (session) {
            server_->DeleteSession(session);
        }
    }

    virtual void Destroy() {
        delete parent_;
    }

private:
    XmppServer *server_;
    XmppServerConnection *parent_;
};

XmppServerConnection::XmppServerConnection(XmppServer *server,
    const XmppChannelConfig *config)
    : XmppConnection(server, config), 
      duplicate_(false),
      on_work_queue_(false),
      conn_endpoint_(NULL),
      deleter_(new DeleteActor(server, this)),
      server_delete_ref_(this, server->deleter()) {
    assert(!config->ClientOnly());
    XMPP_INFO(XmppConnectionCreate, "Server", FromString(), ToString());
}

XmppServerConnection::~XmppServerConnection() {
    CHECK_CONCURRENCY("bgp::Config");

    XMPP_INFO(XmppConnectionDelete, "Server", FromString(), ToString());
    server()->RemoveDeletedConnection(this);
}

void XmppServerConnection::ManagedDelete() {
    XMPP_UTDEBUG(XmppConnectionDelete, "Managed server connection delete", 
                 FromString(), ToString());
    deleter_->Delete();
}

void XmppServerConnection::RetryDelete() {
    if (!deleter()->IsDeleted())
        return;
    deleter()->RetryDelete();
}

LifetimeManager *XmppServerConnection::lifetime_manager() {
    return server()->lifetime_manager();
}

XmppServer *XmppServerConnection::server() {
    return static_cast<XmppServer *>(server_);
}

LifetimeActor *XmppServerConnection::deleter() {
    return deleter_.get();
}

const LifetimeActor *XmppServerConnection::deleter() const {
    return deleter_.get();
}

void XmppServerConnection::set_close_reason(const string &close_reason) {
    if (conn_endpoint_)
        conn_endpoint_->set_close_reason(close_reason);

    if (!logUVE())
        return;

    XmppPeerInfoData peer_info;
    peer_info.set_name(ToUVEKey());
    peer_info.set_close_reason(close_reason);
    XMPPPeerInfoSend(peer_info);
}

uint32_t XmppServerConnection::flap_count() const {
    return conn_endpoint_ ? conn_endpoint_->flap_count() : 0;
}

void XmppServerConnection::increment_flap_count() {
    XmppConnectionEndpoint *conn_endpoint = conn_endpoint_;
    if (!conn_endpoint)
        conn_endpoint = server()->FindConnectionEndpoint(ToString());
    if (!conn_endpoint)
        return;
    conn_endpoint->increment_flap_count();

    if (!logUVE())
        return;

    XmppPeerInfoData peer_info;
    peer_info.set_name(ToUVEKey());
    PeerFlapInfo flap_info;
    flap_info.set_flap_count(conn_endpoint->flap_count());
    flap_info.set_flap_time(conn_endpoint->last_flap());
    peer_info.set_flap_info(flap_info);
    XMPPPeerInfoSend(peer_info);
}

const std::string XmppServerConnection::last_flap_at() const {
    return conn_endpoint_ ? conn_endpoint_->last_flap_at() : "";
}

void XmppServerConnection::FillShowInfo(
    ShowXmppConnection *show_connection) const {
    show_connection->set_name(ToString());
    show_connection->set_deleted(IsDeleted());
    show_connection->set_remote_endpoint(endpoint_string());
    show_connection->set_local_endpoint(local_endpoint_string());
    show_connection->set_state(StateName());
    show_connection->set_last_event(LastEvent());
    show_connection->set_last_state(LastStateName());
    show_connection->set_last_state_at(LastStateChangeAt());
    show_connection->set_receivers(channel_mux()->GetReceiverList());
    show_connection->set_server_auth_type(GetXmppAuthenticationType());
    show_connection->set_dscp_value(dscp_value());
}

class XmppClientConnection::DeleteActor : public LifetimeActor {
public:
    DeleteActor(XmppClient *client, XmppClientConnection *parent)
        : LifetimeActor(client->lifetime_manager()),
          client_(client), parent_(parent) {
    }

    virtual bool MayDelete() const {
        return parent_->MayDelete();
    }

    virtual void Shutdown() {
        if (parent_->session()) {
            client_->NotifyConnectionEvent(parent_->ChannelMux(),
                xmps::NOT_READY);
        }

        XmppSession *session = NULL;
        if (parent_->state_machine()) {
            session = parent_->state_machine()->session();
            parent_->state_machine()->clear_session();
        }
        if (session) {
            client_->DeleteSession(session);
        }
    }

    virtual void Destroy() {
        delete parent_;
    }

private:
    XmppClient *client_;
    XmppClientConnection *parent_;
};

XmppClientConnection::XmppClientConnection(XmppClient *server,
    const XmppChannelConfig *config)
    : XmppConnection(server, config), 
      flap_count_(0),
      deleter_(new DeleteActor(server, this)),
      server_delete_ref_(this, server->deleter()) {
    assert(config->ClientOnly());
    XMPP_UTDEBUG(XmppConnectionCreate, "Client", FromString(), ToString());
}

XmppClientConnection::~XmppClientConnection() {
    CHECK_CONCURRENCY("bgp::Config");

    XMPP_INFO(XmppConnectionDelete, "Client", FromString(), ToString());
    server()->RemoveConnection(this);
}

void XmppClientConnection::ManagedDelete() {
    XMPP_UTDEBUG(XmppConnectionDelete, "Managed Client Delete", 
                 FromString(), ToString());
    deleter_->Delete();
}

void XmppClientConnection::RetryDelete() {
    if (!deleter()->IsDeleted())
        return;
    deleter()->RetryDelete();
}

LifetimeManager *XmppClientConnection::lifetime_manager() {
    return server()->lifetime_manager();
}

XmppClient *XmppClientConnection::server() {
    return static_cast<XmppClient *>(server_);
}

LifetimeActor *XmppClientConnection::deleter() {
    return deleter_.get();
}

const LifetimeActor *XmppClientConnection::deleter() const {
    return deleter_.get();
}

void XmppClientConnection::set_close_reason(const string &close_reason) {
    close_reason_ = close_reason;
    if (!logUVE())
        return;

    XmppPeerInfoData peer_info;
    peer_info.set_name(ToUVEKey());
    peer_info.set_close_reason(close_reason_);
    XMPPPeerInfoSend(peer_info);
}

uint32_t XmppClientConnection::flap_count() const {
    return flap_count_;
}

void XmppClientConnection::increment_flap_count() {
    flap_count_++;
    last_flap_ = UTCTimestampUsec();

    if (!logUVE())
        return;

    XmppPeerInfoData peer_info;
    peer_info.set_name(ToUVEKey());
    PeerFlapInfo flap_info;
    flap_info.set_flap_count(flap_count_);
    flap_info.set_flap_time(last_flap_);
    peer_info.set_flap_info(flap_info);
    XMPPPeerInfoSend(peer_info);
}

const std::string XmppClientConnection::last_flap_at() const {
    return last_flap_ ? integerToString(UTCUsecToPTime(last_flap_)) : "";
}

XmppConnectionEndpoint::XmppConnectionEndpoint(const string &client)
    : client_(client), flap_count_(0), last_flap_(0), connection_(NULL) {
}

void XmppConnectionEndpoint::set_close_reason(const string &close_reason) {
    close_reason_ = close_reason;
}

uint32_t XmppConnectionEndpoint::flap_count() const {
    return flap_count_;
}

void XmppConnectionEndpoint::increment_flap_count() {
    flap_count_++;
    last_flap_ = UTCTimestampUsec();
}

uint64_t XmppConnectionEndpoint::last_flap() const {
    return last_flap_;
}

const std::string XmppConnectionEndpoint::last_flap_at() const {
    return last_flap_ ? integerToString(UTCUsecToPTime(last_flap_)) : "";
}

XmppConnection *XmppConnectionEndpoint::connection() {
    return connection_;
}

const XmppConnection *XmppConnectionEndpoint::connection() const {
    return connection_;
}

void XmppConnectionEndpoint::set_connection(XmppConnection *connection) {
    assert(!connection_);
    connection_ = connection;
}

void XmppConnectionEndpoint::reset_connection() {
    assert(connection_);
    connection_ = NULL;
}
