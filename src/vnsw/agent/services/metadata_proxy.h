/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_metadata_proxy_h_
#define vnsw_agent_metadata_proxy_h_

class EventManager;
class HttpSession;
class HttpRequest;
class HttpServer;

class MetadataProxy {
public:
    struct SessionData {
        HttpConnection *conn;
        uint32_t content_len;
        uint32_t data_sent;
        bool close_req;
        bool header_end;

        SessionData(HttpConnection *c, bool conn_close) 
            : conn(c), content_len(0), data_sent(0),
              close_req(conn_close), header_end(false) {}
    };

    typedef std::map<HttpSession *, SessionData> SessionMap;
    typedef std::pair<HttpSession *, SessionData> SessionPair;
    typedef std::map<HttpConnection *, HttpSession *> ConnectionSessionMap;
    typedef std::pair<HttpConnection *, HttpSession *> ConnectionSessionPair;

    MetadataProxy(ServicesModule *module, const std::string &secret);
    virtual ~MetadataProxy();
    void Shutdown();

    void HandleMetadataRequest(HttpSession *session, const HttpRequest *request);
    void HandleMetadataResponse(HttpConnection *conn, HttpSession *session,
                                std::string &msg, boost::system::error_code &ec);

    void OnServerSessionEvent(HttpSession *session, TcpSession::Event event);
    void OnClientSessionEvent(HttpClientSession *session, TcpSession::Event event);

private:
    HttpConnection *GetProxyConnection(HttpSession *session, bool conn_close);
    void CloseServerSession(HttpSession *session);
    void CloseClientSession(HttpConnection *conn);
    void ErrorClose(HttpSession *sesion);

    ServicesModule *services_;
    std::string shared_secret_;
    HttpServer *http_server_;
    HttpClient *http_client_;
    SessionMap metadata_sessions_;
    ConnectionSessionMap metadata_proxy_sessions_;
    tbb::mutex mutex_;

    DISALLOW_COPY_AND_ASSIGN(MetadataProxy);
};

#endif // vnsw_agent_metadata_proxy_h_
