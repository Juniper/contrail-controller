/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __XMPP_SESSION_H__
#define __XMPP_SESSION_H__

#include <string>
#include <boost/regex.hpp>
#include "io/tcp_server.h"
#include "io/tcp_session.h"

class XmppStream;
class XmppServer;
class XmppConnection;
class XmppRegexMock;

class XmppSession : public TcpSession {
public:
    XmppSession(TcpServer *server, Socket *sock, bool async_ready = true);
    virtual ~XmppSession();

    void SetConnection(XmppConnection *connection) {
        this->connection_ = connection; 
    }
    XmppConnection *Connection() { return connection_; }
     
    XmppStream *SessionStream() { return stream_; }
    void SessionStreamSet(XmppStream *strm) { this->stream_ = strm;}
    virtual void WriteReady(const boost::system::error_code &error);

    typedef std::pair<uint64_t, uint64_t> StatsPair; // (packets, bytes)
    StatsPair Stats(unsigned int message_type) const;
    void IncStats(unsigned int message_type, uint64_t bytes);

    static const int kMaxMessageSize = 4096;
    friend class XmppRegexMock;
   
protected:
    std::string jid;
    virtual void OnRead(Buffer buffer);
    
private:
    typedef std::deque<Buffer> BufferQueue;

    boost::regex tag_to_pattern(const char *); 
    int MatchRegex(const boost::regex &patt);
    bool Match(Buffer buffer, int *result, bool NewBuf);
    void SetBuf(const std::string &);
    void ReplaceBuf(const std::string &);
    bool LeftOver() const;

    XmppConnection *connection_;
    BufferQueue queue_;
    XmppStream *stream_;
    std::string begin_tag_;
    std::string buf_;
    std::string::const_iterator offset_;
    int tag_known_;
    boost::match_results<std::string::const_iterator> res_;
    std::vector<StatsPair> stats_; // packet count

    static const boost::regex patt_;
    static const boost::regex stream_patt_;
    static const boost::regex stream_res_end_;
    static const boost::regex whitespace_;

    DISALLOW_COPY_AND_ASSIGN(XmppSession);
};

class XmppStream {
public:

    enum XmppStreamNS {
        JABBER_CLIENT = 1,
        JABBER_SERVER = 2
    };

    XmppStream(std::string resource, XmppStreamNS ns) 
        : resource(resource), ns(ns) {
    }
    std::string xmppStreamFQDNJid();
    static const std::string close_string;
    std::string resource;
    XmppStreamNS ns;

private:
    std::string resource_ ;
    XmppStreamNS ns_;
    DISALLOW_COPY_AND_ASSIGN(XmppStream);
};


#endif // __XMPP_SESSION_H__
