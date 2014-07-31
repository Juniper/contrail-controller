/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap_channel.h"
#include <sstream>
#include <string>

#include "base/connection_info.h"
#include "base/logging.h"
#include "base/task.h"
#include "base/task_annotations.h"
#include "base/timer_impl.h"

#include <boost/asio/buffer.hpp>
#include <boost/asio/detail/socket_option.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <boost/asio/connect.hpp>
#include <boost/asio/placeholders.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/system/error_code.hpp>

// the next few only for base64
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/insert_linebreaks.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include <boost/archive/iterators/ostream_iterator.hpp>

#include "ifmap_state_machine.h"
#include "ifmap/ifmap_log.h"
#include "ifmap_manager.h"
#include "ifmap/ifmap_server.h"
#include "ifmap/ifmap_server_show_types.h"
#include "ifmap/ifmap_log_types.h"

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/request_pipeline.h>
#include "bgp/bgp_sandesh.h"

const int IFMapChannel::kSocketCloseTimeout = 2 * 1000;
const uint64_t IFMapChannel::kRetryConnectionMax = 2;

using namespace boost::assign;
using namespace std;
using boost::system::error_code;

static std::string base64_encode(const std::string& text) {
    using namespace boost::archive::iterators;

    std::stringstream os;
    typedef 
        // insert line breaks every 72 characters
        insert_linebreaks<
            // convert binary values ot base64 characters
            base64_from_binary<
                // retrieve 6 bit integers from a sequence of 8 bit bytes
                transform_width<const char *, 6, 8>
            >
            ,72
        >
        base64_text;

    std::copy(base64_text(text.c_str()),
              base64_text(text.c_str() + text.size()),
              boost::archive::iterators::ostream_iterator<char>(os));
    if ((text.length() % 3) == 1) {
        os << "==";
    } else if ((text.length() % 3) == 2) {
        os << "=";
    }
    return os.str();
}

void IFMapChannel::ChannelUseCertAuth(const std::string& certstore)
{
    boost::system::error_code ec;
    string certname;
    char hostname[1024];
    struct addrinfo hints, *info;

    IFMAP_PEER_DEBUG(IFMapServerConnection, "Certificate Store is", certstore);

    // get host FQDN; eg a2s8.contrail.juniper.net
    hostname[sizeof(hostname)-1] = '\0';
    gethostname(hostname, sizeof(hostname)-1);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_CANONNAME;
    if (getaddrinfo(hostname, "http", &hints, &info) != 0)
        return;
    if (info == NULL)
        return;
    strncpy(hostname, info->ai_canonname, sizeof(hostname)-1);
    freeaddrinfo(info);

    // certificate files - follow puppet convention and subdirs
    certname = string(hostname) + ".pem";
    IFMAP_PEER_DEBUG(IFMapServerConnection, "Certificate name is", certname);

    // server auth
    ctx_.set_verify_mode(boost::asio::ssl::context::verify_peer, ec);
    ctx_.load_verify_file(certstore + "/certs/ca.pem", ec);

    // client auth
    ctx_.use_private_key_file(certstore + "/private_keys/" + certname,
        boost::asio::ssl::context_base::pem, ec);
    ctx_.use_certificate_file(certstore + "/certs/" + certname, 
        boost::asio::ssl::context_base::pem, ec);

    return;
}
 
IFMapChannel::IFMapChannel(IFMapManager *manager, const std::string& user,
                const std::string& passwd, const std::string& certstore)
    : manager_(manager), resolver_(*(manager->io_service())),
      ctx_(*(manager->io_service()), boost::asio::ssl::context::sslv3_client),
      io_strand_(*(manager->io_service())),
      ssrc_socket_(new SslStream((*manager->io_service()), ctx_)),
      arc_socket_(new SslStream((*manager->io_service()), ctx_)),
      username_(user), password_(passwd), state_machine_(NULL),
      response_state_(NONE), sequence_number_(0), recv_msg_cnt_(0),
      sent_msg_cnt_(0), reconnect_attempts_(0), connection_status_(NOCONN),
      connection_status_change_at_(UTCTimestampUsec()) {

    boost::system::error_code ec;
    if (certstore.empty()) {
        ctx_.set_verify_mode(boost::asio::ssl::context::verify_none, ec);
    } else {
        ChannelUseCertAuth(certstore);
    }
    string auth_str = username_ + ":" + password_;
    b64_auth_str_ = base64_encode(auth_str);
    IFMAP_PEER_DEBUG(IFMapServerConnection, "Base64 auth string is",
                     b64_auth_str_);
}

void IFMapChannel::set_connection_status(ConnectionStatus status) {
    if (connection_status_ != status) {
        connection_status_ = status;
        connection_status_change_at_ = UTCTimestampUsec();

        // Update connection info
        process::ConnectionState::GetInstance()->Update(
            process::ConnectionType::IFMAP,
            "IFMapServer", connection_status_ == UP ? 
            process::ConnectionStatus::UP :
            process::ConnectionStatus::DOWN,
            endpoint_, "Connection with IFMap Server (irond)");
    }
}

// Will run in the context of the main task
void IFMapChannel::CloseSockets(const boost::system::error_code& error,
                                TimerImpl *socket_close_timer) {
    // operation_aborted is the only possible error. Since we are not going to
    // cancel this timer, we should never really get an error.
    if (error) {
        IFMAP_PEER_WARN(IFMapServerConnection, error.message(), "");
    }

    SslStream *ssrc_socket = ssrc_socket_.release();
    SslStream *arc_socket = arc_socket_.release();
    if (ssrc_socket) {
        delete ssrc_socket;
    }
    if (arc_socket) {
        delete arc_socket;
    }
    if (socket_close_timer) {
        delete socket_close_timer;
    }

    // Create new sockets for the new connection attempt
    ssrc_socket_.reset(new SslStream((*manager_->io_service()), ctx_));
    arc_socket_.reset(new SslStream((*manager_->io_service()), ctx_));

    // If we have tried connecting to this peer enough times, lets try
    // connecting to a new one, if available.
    if (RetryNewHost()) {
        bool use_new = manager_->PeerDown();
        // If a new peer is available, the manager has already queued it up for
        // the SM. Queue the connection-cleaned event for the SM.
        if (use_new) {
            clear_reconnect_attempts();
        }
        // If a new one is not available, continue trying the current peer.
    }
    state_machine_->ProcConnectionCleaned();
}

// Will run in the context of the main task
void IFMapChannel::ReconnectPreparationInMainThr() {
    CHECK_CONCURRENCY_MAIN_THR();
    if (ConnectionStatusIsDown()) {
        IFMAP_PEER_DEBUG(IFMapServerConnection, 
                         "Retrying connection to Ifmap-server.", "");
    } else {
        IFMAP_PEER_DEBUG(IFMapServerConnection,
                         "Connection to Ifmap-server went down.", "");
    }

    increment_reconnect_attempts();
    set_connection_status(DOWN);
    pub_id_ = std::string();
    session_id_ = std::string();
    clear_recv_msg_cnt();
    clear_sent_msg_cnt();

    boost::system::error_code ec;
    ssrc_socket_->next_layer().shutdown(
            boost::asio::ip::tcp::socket::shutdown_both, ec);
    ssrc_socket_->next_layer().close(ec);

    arc_socket_->next_layer().shutdown(
            boost::asio::ip::tcp::socket::shutdown_both, ec);
    arc_socket_->next_layer().close(ec);

    // Create a timer to cleanup the sockets so that CloseSockets() gets called
    // in the main task context.
    // Create the timer on the heap so that we release all resources correctly
    // even when we are called multiple times.
    TimerImpl *socket_close_timer = new TimerImpl(*manager_->io_service());
    socket_close_timer->expires_from_now(kSocketCloseTimeout, ec);
    socket_close_timer->async_wait(
        boost::bind(&IFMapChannel::CloseSockets, this,
                    boost::asio::placeholders::error, socket_close_timer));
}

void IFMapChannel::ReconnectPreparation() {
    CHECK_CONCURRENCY("ifmap::StateMachine");
    io_strand_.post(
        boost::bind(&IFMapChannel::ReconnectPreparationInMainThr, this));
}

// Will run in the context of the main task
void IFMapChannel::DoResolveInMainThr() {
    CHECK_CONCURRENCY_MAIN_THR();
    boost::asio::ip::tcp::resolver::query conn_query = 
        boost::asio::ip::tcp::resolver::query(host_, port_);
    resolver_.async_resolve(conn_query,
                            boost::bind(&IFMapChannel::ReadResolveResponse,
                                        this, boost::asio::placeholders::error,
                                        boost::asio::placeholders::iterator));
}

void IFMapChannel::DoResolve() {
    CHECK_CONCURRENCY("ifmap::StateMachine");
    io_strand_.post(boost::bind(&IFMapChannel::DoResolveInMainThr, this));
}

void IFMapChannel::ReadResolveResponse(const boost::system::error_code& error,
          boost::asio::ip::tcp::resolver::iterator endpoint_iterator) {
    if (!error) {
        endpoint_ = *endpoint_iterator;
    }
    state_machine_->ProcResolveResponse(error);
}

// Will run in the context of the main task
void IFMapChannel::DoConnectInMainThr(bool is_ssrc) {
    CHECK_CONCURRENCY_MAIN_THR();
    SslStream *socket =
        ((is_ssrc == true) ? ssrc_socket_.get() : arc_socket_.get());

    socket->lowest_layer().async_connect(endpoint_,
        boost::bind(&IFMapStateMachine::ProcConnectResponse, state_machine_,
                    boost::asio::placeholders::error));

    // Set the connection as UP only after reaching arc-connect as its more
    // likely everything else will go through since ssrc went through the same
    // steps earlier successfully
    if (!is_ssrc) {
        if (ConnectionStatusIsDown()) {
            sequence_number_++;
            manager_->ifmap_server()->StaleNodesCleanup();
        }
        set_connection_status(UP);
        IFMAP_PEER_DEBUG(IFMapServerConnection,
                         "Connection to Ifmap-server came up.", "");
    }
}

void IFMapChannel::DoConnect(bool is_ssrc) {
    CHECK_CONCURRENCY("ifmap::StateMachine");
    io_strand_.post(
        boost::bind(&IFMapChannel::DoConnectInMainThr, this, is_ssrc));
}

// Will run in the context of the main task
void IFMapChannel::DoSslHandshakeInMainThr(bool is_ssrc) {
    CHECK_CONCURRENCY_MAIN_THR();
    SslStream *socket =
        ((is_ssrc == true) ? ssrc_socket_.get() : arc_socket_.get());

    // handshake as 'client'
    socket->async_handshake(boost::asio::ssl::stream_base::client,
        boost::bind(&IFMapStateMachine::ProcHandshakeResponse, state_machine_,
                    boost::asio::placeholders::error));
    if (!is_ssrc) {
        SetArcSocketOptions();
    }
}

void IFMapChannel::DoSslHandshake(bool is_ssrc) {
    CHECK_CONCURRENCY("ifmap::StateMachine");
    io_strand_.post(
        boost::bind(&IFMapChannel::DoSslHandshakeInMainThr, this, is_ssrc));
}

// Will run in the context of the main task
void IFMapChannel::SendNewSessionRequestInMainThr(std::string ns_str) {
    CHECK_CONCURRENCY_MAIN_THR();
    boost::asio::async_write(*ssrc_socket_.get(),
        boost::asio::buffer(ns_str.c_str(), ns_str.length()),
        boost::bind(&IFMapStateMachine::ProcNewSessionWrite, state_machine_,
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred));
}

void IFMapChannel::SendNewSessionRequest() {
    CHECK_CONCURRENCY("ifmap::StateMachine");
    std::ostringstream body;

    body << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<env:Envelope xmlns:env=\"http://www.w3.org/2003/05/soap-envelope\" xmlns:ifmap=\"http://www.trustedcomputinggroup.org/2010/IFMAP/2\" xmlns:contrail=\"http://www.contrailsystems.com/vnc_cfg.xsd\" xmlns:meta=\"http://www.trustedcomputinggroup.org/2010/IFMAP-METADATA/2\" >\n  <env:Body>\n\t\t<ifmap:newSession />\n  </env:Body>\n</env:Envelope>\n";

    // section 4.1: rfc 2616
    std::ostringstream ns_str;
    ns_str << "POST / HTTP/1.1\r\n";
    ns_str << "Content-length: " << body.str().length() << "\r\n";
    ns_str << "SOAPAction: newSession\r\n";
    ns_str << "User-Agent: control-node\r\n";
    ns_str << "Host: " << host_ << ":" << port_ << "\r\n";
    ns_str << "Content-type: text/xml; charset=\"UTF-8\"\r\n";
    ns_str << "Authorization: Basic " << b64_auth_str_ << "\r\n";
    ns_str << "\r\n";
    ns_str << body.str();

    io_strand_.post(boost::bind(&IFMapChannel::SendNewSessionRequestInMainThr,
                                this, ns_str.str()));
}

// Will run in the context of the main task
void IFMapChannel::NewSessionResponseWaitInMainThr() {
    CHECK_CONCURRENCY_MAIN_THR();
    // Read the http header. Might get extra bytes beyond the header.
    boost::asio::async_read_until(*ssrc_socket_.get(), reply_, "\r\n\r\n",
        boost::bind(&IFMapStateMachine::ProcResponse, state_machine_,
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred));
}

void IFMapChannel::NewSessionResponseWait() {
    CHECK_CONCURRENCY("ifmap::StateMachine");
    response_state_ = NEWSESSION;
    io_strand_.post(
        boost::bind(&IFMapChannel::NewSessionResponseWaitInMainThr, this));
}

// TODO search for errorResult, interpret return -1
int IFMapChannel::ExtractPubSessionId() {

    CHECK_CONCURRENCY("ifmap::StateMachine");
    // Append the new bytes read, if any, to the stringstream
    reply_ss_ << &reply_;
    std::string reply_str = reply_ss_.str();
    IFMAP_PEER_DEBUG(IFMapServerConnection,
                     "PubSessionId message is: \n", reply_str);

    if ((reply_str.find("errorResult") != string::npos) ||
        (reply_str.find("endSessionResult") != string::npos)) {
        IFMAP_PEER_WARN(IFMapServerConnection, 
                       "Error received instead of PubSessionId. Quitting.", "");
        return -1;
    }

    // we must have the newSessionResult tag
    string ns_result("newSessionResult");
    if (reply_str.find(ns_result) == string::npos) {
        IFMAP_PEER_WARN(IFMapServerConnection,
                        "NewSessionResult missing. Quitting.", "");
        return -1;
    }

    // Get the publisher-id returned by the server
    // EG: ifmap-publisher-id="test2--1870931914-1"

    // get location to the start of the [ifmap-publisher-id="] string
    string str("ifmap-publisher-id=\"");
    size_t pos = reply_str.find(str);
    if (pos == string::npos) {
        IFMAP_PEER_WARN(IFMapServerConnection,
                        "Publisher-id missing. Quitting.", "");
        return -1;
    }

    // get location of the first quote (") beyond ["ifmap-publisher-id="]. This
    // is the location of the end of the pub-id
    string str1("\"");
    size_t pos1 = reply_str.find(str1, pos + str.length());
    if (pos1 == string::npos) {
        IFMAP_PEER_WARN(IFMapServerConnection,
                        "Slash missing after Publisher-id. Quitting.", "");
        return -1;
    }

    size_t start_pos = (pos + str.length());
    size_t pub_id_len = pos1 - pos - str.length();
    pub_id_ = reply_str.substr(start_pos, pub_id_len);
    IFMAP_PEER_DEBUG(IFMapServerConnection, "Pub-id is", pub_id_);

    // Get the session-id returned by the server
    // EG: session-id="2077221532-423634091-1596075545-1209811427"

    // get location to the start of the [session-id="] string
    str = string("session-id=\"");
    pos = reply_str.find(str);
    if (pos == string::npos) {
        IFMAP_PEER_WARN(IFMapServerConnection, "Session-id missing. Quitting.",
                        "");
        return -1;
    }

    // get location of the first quote (") beyond ["session-id="]. This
    // is the location of the end of the session-id
    str1 = string("\"");
    pos1 = reply_str.find(str1, pos + str.length());
    if (pos1 == string::npos) {
        IFMAP_PEER_WARN(IFMapServerConnection,
                        "Slash missing after Session-id. Quitting.", "");
        return -1;
    }

    start_pos = (pos + str.length());
    size_t session_id_len = pos1 - pos - str.length();
    session_id_ = reply_str.substr(start_pos, session_id_len);
    IFMAP_PEER_DEBUG(IFMapServerConnection, "Session-id is", session_id_);

    return 0;
}

// Will run in the context of the main task
void IFMapChannel::SendSubscribeInMainThr(std::string sub_msg) {
    CHECK_CONCURRENCY_MAIN_THR();
    boost::asio::async_write(*ssrc_socket_.get(),
        boost::asio::buffer(sub_msg.c_str(), sub_msg.length()),
        boost::bind(&IFMapStateMachine::ProcSubscribeWrite, state_machine_,
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred));
}

void IFMapChannel::SendSubscribe() {
    CHECK_CONCURRENCY("ifmap::StateMachine");
    std::ostringstream body;
    body << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<env:Envelope xmlns:env=\"http://www.w3.org/2003/05/soap-envelope\" xmlns:ifmap=\"http://www.trustedcomputinggroup.org/2010/IFMAP/2\" xmlns:contrail=\"http://www.contrailsystems.com/vnc_cfg.xsd\" xmlns:meta=\"http://www.trustedcomputinggroup.org/2010/IFMAP-METADATA/2\" >\n <env:Body>\n\t\t<ifmap:subscribe session-id=\"";
    body << session_id_;
    body << "\"><update name=\"root\" max-depth=\"255\"><identity name=\"contrail:config-root:root\" type=\"other\" other-type-definition=\"extended\"/></update></ifmap:subscribe></env:Body>\n</env:Envelope>\n";

    // section 4.1: rfc 2616
    std::ostringstream sub_msg;
    sub_msg << "POST / HTTP/1.1\r\n";
    sub_msg << "Content-length: " << body.str().length() << "\r\n";
    sub_msg << "Content-type: text/xml; charset=\"UTF-8\"\r\n";
    sub_msg << "Authorization: Basic " << b64_auth_str_ << "\r\n";
    sub_msg << "SOAPAction: subscribe\r\n";
    sub_msg << "\r\n";
    sub_msg << body.str();

    io_strand_.post(boost::bind(&IFMapChannel::SendSubscribeInMainThr, this,
                                sub_msg.str()));
}

// Will run in the context of the main task
void IFMapChannel::SubscribeResponseWaitInMainThr() {
    CHECK_CONCURRENCY_MAIN_THR();
    // Read the http header. Might get extra bytes beyond the header.
    boost::asio::async_read_until(*ssrc_socket_.get(), reply_, "\r\n\r\n",
        boost::bind(&IFMapStateMachine::ProcResponse, state_machine_,
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred));
}

void IFMapChannel::SubscribeResponseWait() {
    // Read the http header. Might get extra bytes beyond the header.
    CHECK_CONCURRENCY("ifmap::StateMachine");
    response_state_ = SUBSCRIBE;
    io_strand_.post(
        boost::bind(&IFMapChannel::SubscribeResponseWaitInMainThr, this));
}

int IFMapChannel::ReadSubscribeResponseStr() {

    CHECK_CONCURRENCY("ifmap::StateMachine");
    // Append the new bytes read, if any, to the stringstream
    reply_ss_ << &reply_;
    std::string reply_str = reply_ss_.str();
    IFMAP_PEER_DEBUG(IFMapServerConnection,
                     "SubscribeResponse message is: \n", reply_str);

    // TODO get ErrorResultType if it helps debugging
    // <xsd:element name="errorResult" type="ErrorResultType"/>

    if ((reply_str.find("errorResult") != string::npos) ||
        (reply_str.find("endSessionResult") != string::npos)) {
        IFMAP_PEER_WARN(IFMapServerConnection,
            "Error received instead of SubscribeReceived. Quitting.", "");
        return -1;
    } else if (reply_str.find(string("subscribeReceived")) != string::npos) {
        return 0;
    } else {
        assert(0);
    }
}

// Will run in the context of the main task
void IFMapChannel::SendPollRequestInMainThr(std::string poll_msg) {
    CHECK_CONCURRENCY_MAIN_THR();
    boost::asio::async_write(*arc_socket_.get(),
        boost::asio::buffer(poll_msg.c_str(), poll_msg.length()),
        boost::bind(&IFMapStateMachine::ProcPollWrite, state_machine_,
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred));
}

void IFMapChannel::SendPollRequest() {
    CHECK_CONCURRENCY("ifmap::StateMachine");
    std::ostringstream body;
    body << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<env:Envelope xmlns:env=\"http://www.w3.org/2003/05/soap-envelope\" xmlns:ifmap=\"http://www.trustedcomputinggroup.org/2010/IFMAP/2\" xmlns:contrail=\"http://www.contrailsystems.com/vnc_cfg.xsd\" xmlns:meta=\"http://www.trustedcomputinggroup.org/2010/IFMAP-METADATA/2\" >\n <env:Body>\n\t\t<ifmap:poll session-id=\"";
    body << session_id_ << "\">";
    body << "</ifmap:poll></env:Body>\n</env:Envelope>\n";

    // section 4.1: rfc 2616
    std::ostringstream poll_msg;
    poll_msg << "POST / HTTP/1.1\r\n";
    poll_msg << "Content-length: " << body.str().length() << "\r\n";
    poll_msg << "Content-type: text/xml; charset=\"UTF-8\"\r\n";
    poll_msg << "Authorization: Basic " << b64_auth_str_ << "\r\n";
    poll_msg << "SOAPAction: poll\r\n";
    poll_msg << "\r\n";
    poll_msg << body.str();

    io_strand_.post(boost::bind(&IFMapChannel::SendPollRequestInMainThr, this,
                                poll_msg.str()));
}

// Will run in the context of the main task
void IFMapChannel::PollResponseWaitInMainThr() {
    CHECK_CONCURRENCY_MAIN_THR();
    boost::asio::async_read_until(*arc_socket_.get(), reply_, "\r\n\r\n",
        boost::bind(&IFMapStateMachine::ProcResponse, state_machine_,
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred));
}

void IFMapChannel::PollResponseWait() {
    CHECK_CONCURRENCY("ifmap::StateMachine");
    // Read the http header. Might get extra bytes beyond the header.
    response_state_ = POLLRESPONSE;
    io_strand_.post(
        boost::bind(&IFMapChannel::PollResponseWaitInMainThr, this));
}

int IFMapChannel::ReadPollResponse() {

    CHECK_CONCURRENCY("ifmap::StateMachine");
    // Append the new bytes read, if any, to the stringstream
    reply_ss_ << &reply_;
    std::string reply_str = reply_ss_.str();
    IFMAP_PEER_LOG_POLL_RESP(IFMapServerConnection,
                   GetSizeAsString(reply_.size(), " bytes in reply_. ") +
                   GetSizeAsString(reply_str.size(), " bytes in reply_str. ") +
                   "PollResponse message is: \n", reply_str);

    // all possible responses, 3.7.5
    if ((reply_str.find("errorResult") != string::npos) ||
        (reply_str.find("endSessionResult") != string::npos)) {
        IFMAP_PEER_WARN(IFMapServerConnection, 
                        "Error received instead of PollResult. Quitting.", "");
        return -1;
    } else if (reply_str.find(string("pollResult")) != string::npos) {
        size_t pos = reply_str.find(string("<?xml version="));
        assert(pos != string::npos);
        string poll_string = reply_str.substr(pos);
        increment_recv_msg_cnt();
        bool success = true;
        if (manager_->pollreadcb()) {
            success = (manager_->pollreadcb())(poll_string.c_str(),
                           poll_string.length(), sequence_number_);
        }
        response_state_ = NONE;
        if (success) {
            return 0;
        } else {
            return -1;
        }
    } else {
        assert(0);
    }
}

// Will run in the context of the main task
void IFMapChannel::ProcResponseInMainThr(size_t bytes_to_read) {
    CHECK_CONCURRENCY_MAIN_THR();
    SslStream *socket = GetSocket(response_state_);
    ProcCompleteMsgCb callback = GetCallback(response_state_);
    boost::asio::async_read(*socket, reply_,
        boost::asio::transfer_exactly(bytes_to_read), callback);
}

void IFMapChannel::ProcResponse(const boost::system::error_code& error,
                                size_t header_length) {
    CHECK_CONCURRENCY("ifmap::StateMachine");
    ProcCompleteMsgCb callback = GetCallback(response_state_);

    if (error) {
        callback(error, header_length);
        return;
    }

    // Reset the buffer so that it becomes empty before we read the new msg
    reply_ss_.str(std::string());
    reply_ss_.clear();

    reply_ss_ << &reply_;
    std::string reply_str = reply_ss_.str();

    if (reply_str.find("401 Unauthorized") != string::npos) {
        IFMAP_PEER_WARN(IFMapServerConnection, 
            "Received 401 Unauthorized. Incorrect username/password.", "");
        boost::system::error_code ec(boost::system::errc::connection_refused,
                                     boost::system::system_category());
        callback(ec, header_length);
        return;
    }

    // From the header, get the content-length i.e. length of the body portion
    // EG: [Content-Length: 23517] (followed by \r\n)
    string srch1("Content-Length: ");
    size_t pos1 = reply_str.find(srch1);
    if (pos1 == string::npos) {
        IFMAP_PEER_WARN(IFMapServerConnection,
                        "No Content-Length found. Improper message.", "");
        boost::system::error_code ec(boost::system::errc::bad_message,
                                     boost::system::system_category());
        callback(ec, header_length);
        return;
    }

    string srch2("\r\n");
    size_t pos2 = reply_str.find(srch2, pos1 + srch1.length());
    if (pos2 == string::npos) {
        IFMAP_PEER_WARN(IFMapServerConnection,
                        "No CRLF found. Improper message.", "");
        boost::system::error_code ec(boost::system::errc::bad_message,
                                     boost::system::system_category());
        callback(ec, header_length);
        return;
    }

    size_t start_pos = (pos1 + srch1.length());
    size_t lenlen = pos2 - pos1 - srch1.length();
    string content_len_str = reply_str.substr(start_pos, lenlen);
    int content_len = atoi(content_len_str.c_str());
    IFMAP_PEER_DEBUG(IFMapChannelProcResp, "Http header length is",
                     header_length, "Content length is", content_len,
                     "Total bytes read are", reply_str.length());

    // If both header and body are completely read, goto the next state
    if ((header_length + content_len) == reply_str.length()) {
        callback(error, header_length);
    } else {
        // Make a request to read the remaining bytes of the body
        // Goto the next state only after finishing the complete read
        size_t bytes_to_read = content_len - 
                                (reply_str.length() - header_length);
        io_strand_.post(boost::bind(&IFMapChannel::ProcResponseInMainThr, this,
                                    bytes_to_read));
    }
}

IFMapChannel::SslStream *IFMapChannel::GetSocket(ResponseState response_state) {
    switch (response_state) {
    case NEWSESSION:
        return ssrc_socket_.get();
    case SUBSCRIBE:
        return ssrc_socket_.get();
    case POLLRESPONSE:
        return arc_socket_.get();
    default:
        assert(0);
    }
}

IFMapChannel::ProcCompleteMsgCb IFMapChannel::GetCallback(
        ResponseState response_state) {
    switch (response_state) {
    case NEWSESSION:
        return boost::bind(&IFMapStateMachine::ProcNewSessionResponse,
                           state_machine_, _1, _2);
    case SUBSCRIBE:
        return boost::bind(&IFMapStateMachine::ProcSubscribeResponse,
                           state_machine_, _1, _2);
    case POLLRESPONSE:
        return boost::bind(&IFMapStateMachine::ProcPollResponseRead,
                           state_machine_, _1, _2);
    default:
        assert(0);
    }
}

// Get the TCP layer and set the keepalive options on it
void IFMapChannel::SetArcSocketOptions() {
    boost::system::error_code ec;

    boost::asio::socket_base::keep_alive option(true);
    arc_socket_->next_layer().set_option(option, ec);
    if (ec) {
        IFMAP_PEER_WARN(IFMapServerConnection, "Error setting keepalive option",
                        ec.message());
    }

#ifdef TCP_KEEPIDLE
    boost::asio::detail::socket_option::integer<IPPROTO_TCP, TCP_KEEPIDLE>
        keepalive_idle_time_option(kSessionKeepaliveIdleTime);
    arc_socket_->next_layer().set_option(keepalive_idle_time_option, ec);
    if (ec) {
        IFMAP_PEER_WARN(IFMapServerConnection,
                        "Error setting keepalive idle time", ec.message());
    }
#endif

#ifdef TCP_KEEPALIVE
    boost::asio::detail::socket_option::integer<IPPROTO_TCP, TCP_KEEPALIVE>
        keepalive_idle_time_option(kSessionKeepaliveIdleTime);
    arc_socket_->next_layer().set_option(keepalive_idle_time_option, ec);
    if (ec) {
        IFMAP_PEER_WARN(IFMapServerConnection,
                        "Error setting keepalive idle time", ec.message());
    }
#endif

#ifdef TCP_KEEPINTVL
    boost::asio::detail::socket_option::integer<IPPROTO_TCP, TCP_KEEPINTVL>
        keepalive_interval_option(kSessionKeepaliveInterval);
    arc_socket_->next_layer().set_option(keepalive_interval_option, ec);
    if (ec) {
        IFMAP_PEER_WARN(IFMapServerConnection,
                        "Error setting keepalive interval", ec.message());
    }
#endif

#ifdef TCP_KEEPCNT
    boost::asio::detail::socket_option::integer<IPPROTO_TCP, TCP_KEEPCNT>
        keepalive_count_option(kSessionKeepaliveProbes);
    arc_socket_->next_layer().set_option(keepalive_count_option, ec);
    if (ec) {
        IFMAP_PEER_WARN(IFMapServerConnection, "Error setting keepalive probes",
                        ec.message());
    }
#endif
}

// The connection to the peer 'host_' has timed-out. Create a new entry for
// this peer or update the peer's entry if it already exists.
void IFMapChannel::IncrementTimedout() {
    string key = host_ + ":" + port_;
    TimedoutMap::iterator iter = timedout_map_.find(key);
    if (iter == timedout_map_.end()) {
        PeerTimedoutInfo info(1, UTCTimestampUsec());
        timedout_map_.insert(make_pair(key, info));
    } else {
        PeerTimedoutInfo info = iter->second;
        ++info.timedout_cnt;
        info.last_timeout_at = UTCTimestampUsec();
        iter->second = info;
    }
}

IFMapChannel::PeerTimedoutInfo IFMapChannel::GetTimedoutInfo(const string &host,
                                                        const string &port) {
    PeerTimedoutInfo timedout_info;
    string key = host + ":" + port;
    TimedoutMap::iterator iter = timedout_map_.find(key);
    if (iter != timedout_map_.end()) {
        timedout_info = iter->second;
    }
    return timedout_info;
}

string IFMapChannel::timeout_to_string(uint64_t timeout) {
    return duration_usecs_to_string(UTCTimestampUsec() - timeout);
}

void IFMapChannel::GetTimedoutEntries(IFMapPeerTimedoutEntries *entries) {
    entries->list_count = timedout_map_.size();
    entries->timedout_list.reserve(entries->list_count);
    for (TimedoutMap::iterator iter = timedout_map_.begin();
         iter != timedout_map_.end(); ++iter) {
        PeerTimedoutInfo info = iter->second;

        IFMapPeerTimedoutEntry entry;
        entry.set_peer(iter->first);
        entry.set_timeout_count(info.timedout_cnt);
        entry.set_last_timeout_ago(timeout_to_string(info.last_timeout_at));

        entries->timedout_list.push_back(entry);
    }
}

static bool IFMapServerInfoHandleRequest(const Sandesh *sr,
                                         const RequestPipeline::PipeSpec ps,
                                         int stage, int instNum,
                                         RequestPipeline::InstData *data) {
    const IFMapPeerServerInfoReq *request =
        static_cast<const IFMapPeerServerInfoReq *>(ps.snhRequest_.get());
    BgpSandeshContext *bsc = 
        static_cast<BgpSandeshContext *>(request->client_context());

    IFMapManager *ifmap_manager = bsc->ifmap_server->get_ifmap_manager();
    IFMapChannel *channel = ifmap_manager->channel();
    IFMapStateMachine *sm = ifmap_manager->state_machine();

    IFMapPeerServerInfo server_info;
    IFMapPeerServerConnInfo server_conn_info;
    IFMapPeerServerStatsInfo server_stats;
    IFMapStateMachineInfo sm_info;
    IFMapDSPeerInfo ds_peer_info;

    server_info.set_url(ifmap_manager->get_url());
    server_info.set_init_done(ifmap_manager->get_init_done());

    server_conn_info.set_publisher_id(channel->get_publisher_id());
    server_conn_info.set_session_id(channel->get_session_id());
    server_conn_info.set_sequence_number(channel->get_sequence_number());
    server_conn_info.set_connection_status(
        channel->get_connection_status_and_time());
    server_conn_info.set_host(channel->get_host());
    server_conn_info.set_port(channel->get_port());

    server_stats.set_rx_msgs(channel->get_recv_msg_cnt());
    server_stats.set_tx_msgs(channel->get_sent_msg_cnt());
    server_stats.set_reconnect_attempts(channel->get_reconnect_attempts());

    IFMapPeerTimedoutEntries timedout_entries;
    channel->GetTimedoutEntries(&timedout_entries);
    server_stats.set_timedout_entries(timedout_entries);

    sm_info.set_state(sm->StateName());
    sm_info.set_last_state(sm->LastStateName());
    sm_info.set_last_state_change_at(sm->last_state_change_at());
    sm_info.set_last_event(sm->last_event());
    sm_info.set_last_event_at(sm->last_event_at());
    sm_info.set_workq_enqueues(sm->WorkQueueEnqueues());
    sm_info.set_workq_dequeues(sm->WorkQueueDequeues());
    sm_info.set_workq_length(sm->WorkQueueLength());

    ifmap_manager->GetAllDSPeerInfo(&ds_peer_info);

    IFMapPeerServerInfoResp *response = new IFMapPeerServerInfoResp();
    response->set_server_info(server_info);
    response->set_server_conn_info(server_conn_info);
    response->set_stats_info(server_stats);
    response->set_sm_info(sm_info);
    response->set_ds_peer_info(ds_peer_info);

    response->set_context(request->context());
    response->set_more(false);
    response->Response();

    // Return 'true' so that we are not called again
    return true;
}

void IFMapPeerServerInfoReq::HandleRequest() const {
    RequestPipeline::StageSpec s0;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s0.taskId_ = scheduler->GetTaskId("ifmap::StateMachine");
    s0.cbFn_ = IFMapServerInfoHandleRequest;
    s0.instances_.push_back(0);

    RequestPipeline::PipeSpec ps(this);
    ps.stages_= list_of(s0);
    RequestPipeline rp(ps);
}

