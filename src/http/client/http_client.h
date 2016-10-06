/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __HTTP_CLIENT_H__
#define __HTTP_CLIENT_H__

#include <boost/asio/ip/tcp.hpp>
#include <boost/function.hpp>
#include <boost/ptr_container/ptr_map.hpp>
#include <boost/system/error_code.hpp>
#include <string>
#include "base/queue_task.h"
#include "base/timer.h"
#include "io/tcp_server.h"
#include "io/tcp_session.h"
#include "http_parser/http_parser.h"

class LifetimeActor;
class LifetimeManager;
class HttpClient;
class HttpConnection;
struct _ConnInfo;
struct _GlobalInfo;

enum Event {
    EVENT_NONE,
    ACCEPT,
    CONNECT_COMPLETE,
    CONNECT_FAILED,
    CLOSE
};

typedef boost::function<void()> EnqueuedCb;
class HttpClientSession : public TcpSession {
public:
    typedef boost::function<void(HttpClientSession *session,
                                 TcpSession::Event event)> SessionEventCb;
    typedef boost::intrusive_ptr<TcpSession> TcpSessionPtr;

    HttpClientSession(HttpClient *client, Socket *socket);
    virtual ~HttpClientSession() { assert(delete_called_ != 0xdeadbeaf); delete_called_ = 0xdeadbeaf; }
    virtual void OnRead(Buffer buffer);
    void RegisterEventCb(SessionEventCb cb);

    void SetConnection(HttpConnection *conn) { connection_ = conn; }
    HttpConnection *Connection() { return connection_; }
    tbb::mutex &mutex() { return mutex_; }

private:
    void OnEvent(TcpSession *session, Event event);
    void OnEventInternal(TcpSessionPtr session, Event event);
    HttpConnection *connection_;
    uint32_t delete_called_;
    tbb::mutex mutex_;
    SessionEventCb event_cb_;

    DISALLOW_COPY_AND_ASSIGN(HttpClientSession);
};

class HttpConnection {
public:
    HttpConnection(boost::asio::ip::tcp::endpoint, size_t id, HttpClient *);
    ~HttpConnection();

    int Initialize();

    typedef boost::function<void(std::string &, boost::system::error_code &)> HttpCb;

    int HttpPut(const std::string &put_string, const std::string &path, HttpCb);
    int HttpPut(const std::string &put_string, const std::string &path,
                bool header, bool short_timeout, bool reuse,
                std::vector<std::string> &hdr_options, HttpCb cb);
    int HttpPost(const std::string &post_string, const std::string &path,
                HttpCb);
    int HttpPost(const std::string &post_string, const std::string &path,
                 bool header, bool short_timeout, bool reuse,
                 std::vector<std::string> &hdr_options, HttpCb cb);
    int HttpGet(const std::string &path, HttpCb);
    int HttpGet(const std::string &path, bool header, bool short_timeout,
                bool reuse, std::vector<std::string> &hdr_options, HttpCb cb);
    int HttpHead(const std::string &path, bool header, bool short_timeout,
                bool reuse, std::vector<std::string> &hdr_options, HttpCb cb);
    int HttpDelete(const std::string &path, HttpCb);
    int HttpDelete(const std::string &path, bool header, bool short_timeout,
                   bool reuse, std::vector<std::string> &hdr_options,
                   HttpCb cb);
    int Status() { return status_; }
    std::string Version() { return version_; }
    std::string Reason() { return reason_; }
    std::map<std::string, std::string> *Headers() { return &headers_; }
    void ClearCallback();

    struct _ConnInfo *curl_handle() { return curl_handle_; }
    HttpClient *client() { return client_; }
    HttpClientSession *session() { return session_; }
    tbb::mutex &mutex() { return mutex_; }
    boost::asio::ip::tcp::endpoint endpoint() { return endpoint_; }
    size_t id() { return id_; }

    const std::string &GetData();
    void set_curl_handle(struct _ConnInfo *handle) { curl_handle_ = handle; }
    HttpClientSession *CreateSession();
    void set_session(HttpClientSession *session);
    void delete_session();
    void AssignData(const char *ptr, size_t size);
    void AssignHeader(const char *ptr, size_t size);
    void UpdateOffset(size_t bytes);
    size_t GetOffset();
    HttpCb HttpClientCb() { return cb_; }
    void RegisterEventCb(HttpClientSession::SessionEventCb cb) { event_cb_ = cb; }
    void set_use_ssl(bool ssl_flag) { use_ssl_ = ssl_flag; }
    bool use_ssl() { return use_ssl_; }
    void set_client_cert(const std::string &client_cert) {
        client_cert_.assign(client_cert);
    }
    void set_client_cert_type(const std::string &client_cert_type) {
        client_cert_type_.assign(client_cert_type);
    }
    void set_client_key(const std::string &client_key) {
        client_key_.assign(client_key);
    }
    void set_ca_cert(const std::string &ca_cert) {
        ca_cert_.assign(ca_cert);
    }

private:
    std::string make_url(std::string &path);

    unsigned short bool2bf(bool header, bool short_timeout, bool reuse) {
        return (header ? 1 << 2 : 0) | (short_timeout ? 1 << 1 : 0) |
               (reuse ? 1 : 0);
    }
    void bf2bool(unsigned short bf, bool &header, bool &short_timeout,
            bool &reuse) {
        header = bf & 4u;
        short_timeout = bf & 2u;
        reuse = bf & 1u;
    }
    void HttpProcessInternal(const std::string body, std::string path,
                             //bool header, bool short_timeout, bool reuse,
                             unsigned short header_shortTimeout_reuse,
                             std::vector<std::string> hdr_options,
                             HttpCb cb, http_method m);

    // key = endpoint_ + id_ 
    boost::asio::ip::tcp::endpoint endpoint_;
    size_t id_; 
    HttpCb cb_;
    size_t offset_;
    std::string buf_;
    struct _ConnInfo *curl_handle_;
    HttpClientSession *session_;
    HttpClient *client_;
    mutable tbb::mutex mutex_;
    HttpClientSession::SessionEventCb event_cb_;
    int status_;
    std::string version_;
    std::string reason_;
    std::map<std::string, std::string> headers_;
    bool sent_hdr_; // backward compatibility
    bool use_ssl_; // ssl flag
    std::string client_cert_; // client certificate path
    std::string client_cert_type_; // certificate type
    std::string client_key_; // client key path
    std::string ca_cert_; // ca certificate path
    enum HTTPHeaderDataState {
        STATUS = 142,
        HEADER,
    } state_;

    DISALLOW_COPY_AND_ASSIGN(HttpConnection);
};

// Http Client class
class HttpClient : public TcpServer {
public:
    static const uint32_t kDefaultTimeout = 1;  // one millisec

    explicit HttpClient(EventManager *evm, std::string task_name=std::string(
                "http client"));
    virtual ~HttpClient();

    void Init();
    void Shutdown();
    void SessionShutdown(); 

    virtual TcpSession *CreateSession();
    HttpConnection *CreateConnection(boost::asio::ip::tcp::endpoint);
    bool AddConnection(HttpConnection *);
    void RemoveConnection(HttpConnection *);


    void ProcessEvent(EnqueuedCb cb);
    struct _GlobalInfo *GlobalInfo() { return gi_; }
    boost::asio::io_service *io_service();

    void StartTimer(long);
    void CancelTimer();

    bool IsErrorHard(const boost::system::error_code &ec);

protected:
    virtual TcpSession *AllocSession(Socket *socket);

private:
    void TimerErrorHandler(std::string name, std::string error); 
    void RemoveConnectionInternal(HttpConnection *);
    bool DequeueEvent(EnqueuedCb);
    void ShutdownInternal(); 

    typedef boost::asio::ip::tcp::endpoint endpoint;
    typedef std::pair<endpoint, size_t> Key;
    typedef boost::ptr_map<Key, HttpConnection> HttpConnectionMap;

    bool TimerCb();
    struct _GlobalInfo *gi_;
    Timer *curl_timer_;
    HttpConnectionMap map_;
    size_t id_;

    WorkQueue<EnqueuedCb> work_queue_;

    DISALLOW_COPY_AND_ASSIGN(HttpClient);
};

#endif
