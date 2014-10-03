/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_metadata_proxy_h_
#define vnsw_agent_metadata_proxy_h_

class EventManager;
class HttpSession;
class HttpRequest;
class MetadataServer;
class MetadataClient;

class MetadataProxy {
public:
    struct SessionData {
        SessionData(HttpConnection *c, bool conn_close) 
            : conn(c), content_len(0), data_sent(0),
              close_req(conn_close), header_end(false) {}

        HttpConnection *conn;
        uint32_t content_len;
        uint32_t data_sent;
        bool close_req;
        bool header_end;
    };

    struct MetadataStats {
        MetadataStats() { Reset(); }
        void Reset() { requests = responses = proxy_sessions = internal_errors = 0; }

        uint32_t requests;
        uint32_t responses;
        uint32_t proxy_sessions;
        uint32_t internal_errors;
    };

    typedef std::map<HttpSession *, SessionData> SessionMap;
    typedef std::pair<HttpSession *, SessionData> SessionPair;
    typedef std::map<HttpConnection *, HttpSession *> ConnectionSessionMap;
    typedef std::pair<HttpConnection *, HttpSession *> ConnectionSessionPair;
    typedef boost::intrusive_ptr<HttpSession> HttpSessionPtr;

    MetadataProxy(ServicesModule *module, const std::string &secret);
    virtual ~MetadataProxy();
    void CloseSessions();
    void Shutdown();

    void HandleMetadataRequest(HttpSession *session, const HttpRequest *request);
    void HandleMetadataResponse(HttpConnection *conn, HttpSessionPtr session,
                                std::string &msg, boost::system::error_code &ec);

    void OnServerSessionEvent(HttpSession *session, TcpSession::Event event);
    void OnClientSessionEvent(HttpClientSession *session, TcpSession::Event event);

    const MetadataStats &metadatastats() const { return metadata_stats_; }
    void ClearStats() { metadata_stats_.Reset(); }

private:
    HttpConnection *GetProxyConnection(HttpSession *session, bool conn_close);
    void CloseServerSession(HttpSession *session);
    void CloseClientSession(HttpConnection *conn);
    void ErrorClose(HttpSession *sesion, uint16_t error);

    ServicesModule *services_;
    std::string shared_secret_;
    MetadataServer *http_server_;
    MetadataClient *http_client_;
    SessionMap metadata_sessions_;
    ConnectionSessionMap metadata_proxy_sessions_;
    MetadataStats metadata_stats_;

    DISALLOW_COPY_AND_ASSIGN(MetadataProxy);
};

#endif // vnsw_agent_metadata_proxy_h_
