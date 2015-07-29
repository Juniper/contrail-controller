/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __XMPP_SESSION_H__
#define __XMPP_SESSION_H__

#include <string>
#include <boost/regex.hpp>
#include "io/ssl_server.h"
#include "io/ssl_session.h"

class XmppServer;
class XmppConnection;
class XmppConnectionManager;
class XmppRegexMock;

class XmppSession : public SslSession {
public:
    XmppSession(XmppConnectionManager *manager, SslSocket *sock,
        bool async_ready = true);
    virtual ~XmppSession();

    void SetConnection(XmppConnection *connection);
    void ClearConnection();
    XmppConnection *Connection() { return connection_; }

    virtual void WriteReady(const boost::system::error_code &error);
    void ProcessWriteReady();

    typedef std::pair<uint64_t, uint64_t> StatsPair; // (packets, bytes)
    StatsPair Stats(unsigned int message_type) const;
    void IncStats(unsigned int message_type, uint64_t bytes);

    static const int kMaxMessageSize = 4096;
    friend class XmppRegexMock;

    virtual int GetSessionInstance() const { return index_; }

    boost::system::error_code EnableSocketKeepalive();

protected:
    std::string jid;
    virtual void OnRead(Buffer buffer);

private:
    // 30 seconds - 15s + (3*5)s, max 30 sec
    // socket empty - idle timeout (30 sec)
    // socket tx buffer not empty - tcp user timeout (30 sec)
    static const int kSessionKeepaliveIdleTime = 15; // in seconds
    static const int kSessionKeepaliveInterval = 3; // in seconds probe interval
    static const int kSessionKeepaliveProbes = 5; // # unack probe
    static const int kSessionTcpUserTimeout = 30000; // ms
    typedef std::deque<Buffer> BufferQueue;

    boost::regex tag_to_pattern(const char *); 
    int MatchRegex(const boost::regex &patt);
    bool Match(Buffer buffer, int *result, bool NewBuf);
    void SetBuf(const std::string &);
    void ReplaceBuf(const std::string &);
    bool LeftOver() const;

    XmppConnectionManager *manager_;
    XmppConnection *connection_;
    BufferQueue queue_;
    std::string begin_tag_;
    std::string buf_;
    std::string::const_iterator offset_;
    int tag_known_;
    int index_;
    boost::match_results<std::string::const_iterator> res_;
    std::vector<StatsPair> stats_; // packet count
    int keepalive_idle_time_;
    int keepalive_interval_;
    int keepalive_probes_;
    int tcp_user_timeout_; 
    bool stream_open_matched_;

    static const boost::regex patt_;
    static const boost::regex stream_patt_;
    static const boost::regex stream_res_end_;
    static const boost::regex whitespace_;
    static const boost::regex stream_features_patt_;
    static const boost::regex starttls_patt_;
    static const boost::regex proceed_patt_;
    static const boost::regex end_patt_;

    DISALLOW_COPY_AND_ASSIGN(XmppSession);
};

#endif // __XMPP_SESSION_H__
