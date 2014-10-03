/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __IFMAP_CHANNEL_H__
#define __IFMAP_CHANNEL_H__

#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated"
#endif
#include <boost/asio/ssl.hpp>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <map>

#include <boost/asio/streambuf.hpp>
#include <boost/function.hpp>

class IFMapStateMachine;
class IFMapManager;
class TimerImpl;
class IFMapPeerTimedoutEntries;

class IFMapChannel {
public:
    struct PeerTimedoutInfo {
        PeerTimedoutInfo() : timedout_cnt(0), last_timeout_at(0) {
        }
        PeerTimedoutInfo(uint64_t cnt, uint64_t timeout) : 
            timedout_cnt(cnt), last_timeout_at(timeout) {
        }
        uint64_t timedout_cnt;
        uint64_t last_timeout_at;
    };
    typedef std::map<std::string, PeerTimedoutInfo> TimedoutMap;
    static const int kSocketCloseTimeout;
    static const uint64_t kRetryConnectionMax;

    IFMapChannel(IFMapManager *manager, const std::string& user,
                 const std::string& passwd, const std::string& certstore);

    virtual ~IFMapChannel() { }

    void set_sm(IFMapStateMachine *state_machine) {
        state_machine_ = state_machine;
    }
    IFMapStateMachine *state_machine() { return state_machine_; }

    void ChannelUseCertAuth(const std::string& url);

    virtual void ReconnectPreparation();

    virtual void DoResolve();

    void ReadResolveResponse(const boost::system::error_code& error,
                boost::asio::ip::tcp::resolver::iterator endpoint_iterator);
    virtual void DoConnect(bool is_ssrc);

    virtual void DoSslHandshake(bool is_ssrc);

    virtual void SendNewSessionRequest();

    virtual void NewSessionResponseWait();

    // return 0 for success and -1 for failure
    virtual int ExtractPubSessionId();

    virtual void SendSubscribe();

    virtual void SubscribeResponseWait();

    // return 0 for success and -1 for failure
    virtual int ReadSubscribeResponseStr();

    virtual void SendPollRequest();

    virtual void PollResponseWait();

    virtual int ReadPollResponse();

    void ProcResponse(const boost::system::error_code& error,
                      size_t header_length);
    uint64_t get_sequence_number() { return sequence_number_; }

    uint64_t get_recv_msg_cnt() { return recv_msg_cnt_; }
    
    uint64_t get_sent_msg_cnt() { return sent_msg_cnt_; }

    uint64_t get_reconnect_attempts() { return reconnect_attempts_; }
    
    std::string get_publisher_id() { return pub_id_; }

    std::string get_session_id() { return session_id_; }

    void increment_recv_msg_cnt() { recv_msg_cnt_++; }

    void increment_sent_msg_cnt() { sent_msg_cnt_++; }

    void increment_reconnect_attempts() { reconnect_attempts_++; }

    bool RetryNewHost() {
        return (reconnect_attempts_ > kRetryConnectionMax) ? true : false;
    }

    void clear_recv_msg_cnt() { recv_msg_cnt_ = 0; }

    void clear_sent_msg_cnt() { sent_msg_cnt_ = 0; }

    void clear_reconnect_attempts() { reconnect_attempts_ = 0; }

    bool ConnectionStatusIsDown() const {
        return (connection_status_ == DOWN) ? true : false;
    }

    std::string get_connection_status() {
        switch (connection_status_) {
        case NOCONN:
            return std::string("No Connection");
        case DOWN:
            return std::string("Down");
        case UP:
            return std::string("Up");
        default:
            break;
        }

        return std::string("Invalid");
    }

    uint64_t get_connection_status_change_at() {
        return connection_status_change_at_;
    }

    std::string get_connection_status_and_time() {
        switch (connection_status_) {
        case NOCONN:
            return std::string("No Connection");
        case DOWN:
            return std::string("Down since ") + 
                   timeout_to_string(connection_status_change_at_);
        case UP:
            return std::string("Up since ") +
                   timeout_to_string(connection_status_change_at_);
        default:
            break;
        }

        return std::string("Invalid");
    }

    void IncrementTimedout();

    void GetTimedoutEntries(IFMapPeerTimedoutEntries *entries);

    const std::string &get_host() { return host_; }
    const std::string &get_port() { return port_; }
    void SetHostPort(const std::string &host, const std::string &port) {
        host_ = host;
        port_ = port;
    }
    PeerTimedoutInfo GetTimedoutInfo(const std::string &host,
                                     const std::string &port);

private:
    // 45 seconds i.e. 30 + (3*5)s
    static const int kSessionKeepaliveIdleTime = 30; // in seconds
    static const int kSessionKeepaliveInterval = 3; // in seconds
    static const int kSessionKeepaliveProbes = 5; // count

    enum ResponseState {
        NONE = 0,
        NEWSESSION = 1,
        SUBSCRIBE = 2,
        POLLRESPONSE = 3
    };
    enum ConnectionStatus {
        NOCONN = 0,
        DOWN = 1,
        UP = 2
    };
    typedef boost::asio::ssl::stream<boost::asio::ip::tcp::socket> SslStream;
    typedef boost::function<void(const boost::system::error_code& error,
                            size_t header_length)
                           > ProcCompleteMsgCb;

    SslStream *GetSocket(ResponseState response_state);
    ProcCompleteMsgCb GetCallback(ResponseState response_state);
    void CloseSockets(const boost::system::error_code& error,
                      TimerImpl *socket_close_timer);
    void SetArcSocketOptions();
    std::string timeout_to_string(uint64_t timeout);
    void set_connection_status(ConnectionStatus status);

    void ReconnectPreparationInMainThr();
    void DoResolveInMainThr();
    void DoConnectInMainThr(bool is_ssrc);
    void DoSslHandshakeInMainThr(bool is_ssrc);
    void SendNewSessionRequestInMainThr(std::string ns_str);
    void NewSessionResponseWaitInMainThr();
    void SendSubscribeInMainThr(std::string sub_msg);
    void SubscribeResponseWaitInMainThr();
    void SendPollRequestInMainThr(std::string poll_msg);
    void PollResponseWaitInMainThr();
    void ProcResponseInMainThr(size_t bytes_to_read);

    IFMapManager *manager_;
    boost::asio::ip::tcp::resolver resolver_;
    boost::asio::ssl::context ctx_;
    boost::asio::strand io_strand_;
    std::auto_ptr<SslStream> ssrc_socket_;
    std::auto_ptr<SslStream> arc_socket_;
    std::string username_;
    std::string password_;
    std::string b64_auth_str_;
    std::string pub_id_;
    std::string session_id_;
    std::string host_;
    std::string port_;
    IFMapStateMachine *state_machine_;
    boost::asio::streambuf reply_;
    std::ostringstream reply_ss_;
    ResponseState response_state_;
    uint64_t sequence_number_;
    uint64_t recv_msg_cnt_;
    uint64_t sent_msg_cnt_;
    uint64_t reconnect_attempts_;
    ConnectionStatus connection_status_;
    uint64_t connection_status_change_at_;
    boost::asio::ip::tcp::endpoint endpoint_;
    TimedoutMap timedout_map_;

    std::string GetSizeAsString(size_t stream_sz, std::string log) {
        std::ostringstream ss;
        ss << stream_sz << log;
        return ss.str();
    }
};

#endif /* __IFMAP_CHANNEL_H__ */
