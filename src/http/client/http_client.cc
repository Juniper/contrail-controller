/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "http_client.h"
#include <boost/bind.hpp>
#include "base/task_annotations.h"
#include "io/event_manager.h"
#include "base/logging.h"
#include "http_curl.h"

using namespace std;
using tbb::mutex;

HttpClientSession::HttpClientSession(HttpClient *client, Socket *socket) 
    : TcpSession(client, socket) , delete_called_(0) {
        set_observer(boost::bind(&HttpClientSession::OnEvent, this, _1, _2));
}

void HttpClientSession::OnRead(Buffer buffer) {
    return;
}

void HttpClientSession::OnEvent(TcpSession *session, Event event) {
    if (connection_) {
        connection_->client()->
            ProcessEvent(boost::bind(&HttpClientSession::OnEventInternal,
                         this, TcpSessionPtr(session), event));
    }
}

void HttpClientSession::OnEventInternal(TcpSessionPtr session, Event event) {
    if (event_cb_ && !event_cb_.empty()) {
        event_cb_(static_cast<HttpClientSession *>(session.get()), event);
    }

    if (event == CLOSE) {
        goto error;
    }
    if (event == ACCEPT) {
        goto error;
    }
    if (event == CONNECT_COMPLETE) {
        goto error;
    }
    if (event == CONNECT_FAILED) {
        goto error;
    }
    if (event == EVENT_NONE) {
        goto error;
    }
    return;

error:
    // Call callback function with error;
    return;
}

void HttpClientSession::RegisterEventCb(SessionEventCb cb) {
    event_cb_ = cb;
}

HttpConnection::HttpConnection(boost::asio::ip::tcp::endpoint ep, size_t id, 
                               HttpClient *client) :
    endpoint_(ep), id_(id), cb_(NULL), offset_(0), curl_handle_(NULL),
    session_(NULL), client_(client) {
}

HttpConnection::~HttpConnection() {
    if (session_) {
        {
            tbb::mutex::scoped_lock lock(session_->mutex());
            session_->SetConnection(NULL);
        }
        client_->DeleteSession(session_);
        set_session(NULL);
    }
}

std::string HttpConnection::make_url(std::string &path) {
    std::ostringstream ret;

    ret << "http://" << endpoint_.address().to_string();
    if (endpoint_.port() != 0) {
        ret << ":" << endpoint_.port();
    }
    ret << "/" << path;

    return ret.str();
}

HttpClientSession *HttpConnection::CreateSession() {
    HttpClientSession *session = 
        static_cast<HttpClientSession *>(client_->CreateSession());
    if (session) {
        session->SetConnection(this);
    }
    return session;
}

void HttpConnection::set_session(HttpClientSession *session) {
    session_ = session;
    if (session && event_cb_ && !event_cb_.empty())
        session->RegisterEventCb(event_cb_);
}

int HttpConnection::HttpGet(const std::string &path, HttpCb cb) {
    std::vector<std::string> hdr_options;
    return HttpGet(path, false, true, hdr_options, cb);
}

int HttpConnection::HttpGet(const std::string &path, bool header, bool timeout,
                            std::vector<std::string> &hdr_options,
                            HttpCb cb) {
    const std::string body;
    client()->ProcessEvent(boost::bind(&HttpConnection::HttpProcessInternal,
                           this, body, path, header, timeout, hdr_options, cb,
                           HTTP_GET));
    return 0;
}

int HttpConnection::HttpHead(const std::string &path, bool header, bool timeout,
                             std::vector<std::string> &hdr_options,
                             HttpCb cb) {
    const std::string body;
    client()->ProcessEvent(boost::bind(&HttpConnection::HttpProcessInternal,
                           this, body, path, header, timeout, hdr_options, cb,
                           HTTP_HEAD));
    return 0;
}

int HttpConnection::HttpPut(const std::string &put_string,
                            const std::string &path,  HttpCb cb) {
    std::vector<std::string> hdr_options;
    return HttpPut(put_string, path, false, true, hdr_options, cb);
}

int HttpConnection::HttpPut(const std::string &put_string,
                            const std::string &path, bool header, bool timeout,
                            std::vector<std::string> &hdr_options,
                            HttpCb cb) {
    client()->ProcessEvent(boost::bind(&HttpConnection::HttpProcessInternal,
                                       this, put_string, path, header, timeout,
                                       hdr_options, cb, HTTP_PUT));
    return 0;
}

int HttpConnection::HttpPost(const std::string &post_string,
                             const std::string &path,  HttpCb cb) {
    std::vector<std::string> hdr_options;
    return HttpPost(post_string, path, false, true, hdr_options, cb);
}

int HttpConnection::HttpPost(const std::string &post_string,
                             const std::string &path, bool header, bool timeout,
                             std::vector<std::string> &hdr_options,
                             HttpCb cb) {
    client()->ProcessEvent(boost::bind(&HttpConnection::HttpProcessInternal,
                           this, post_string, path, header, timeout,
                           hdr_options, cb, HTTP_POST));
    return 0;
}

int HttpConnection::HttpDelete(const std::string &path, HttpCb cb) {
    std::vector<std::string> hdr_options;
    return HttpDelete(path, false, true, hdr_options, cb);
}

int HttpConnection::HttpDelete(const std::string &path, bool header, bool timeout,
                               std::vector<std::string> &hdr_options,
                               HttpCb cb) {
    const std::string body;
    client()->ProcessEvent(boost::bind(&HttpConnection::HttpProcessInternal,
                           this, body, path, header, timeout, hdr_options, cb,
                           HTTP_DELETE));
    return 0;
}

void HttpConnection::ClearCallback() {
   cb_ = NULL; 
}

void HttpConnection::HttpProcessInternal(const std::string body, std::string path,
                                         bool header, bool timeout,
                                         std::vector<std::string> hdr_options,
                                         HttpCb cb, http_method method) {
    if (client()->AddConnection(this) == false) {
        // connection already exists
        return;
    }

    struct _GlobalInfo *gi = client()->GlobalInfo();
    struct _ConnInfo *curl_handle = new_conn(this, gi, header, timeout);
    if (!curl_handle) {
        LOG(DEBUG, "Http : unable to create new connection");
        return;
    }

    curl_handle->connection = this;
    set_curl_handle(curl_handle);

    cb_ = cb;

    std::string url = make_url(path);
    set_url(curl_handle_, url.c_str());

    // Add header options to the get request
    for (uint32_t i = 0; i < hdr_options.size(); ++i)
        set_header_options(curl_handle_, hdr_options[i].c_str());

    switch (method) {
        case HTTP_GET:
            http_get(curl_handle_, gi);
            break;

        case HTTP_HEAD:
            http_head(curl_handle_, gi);
            break;

        case HTTP_POST:
            if (!hdr_options.size()) {
                // if no header options are set, set the content type
                set_header_options(curl_handle_, "Content-Type: application/xml");
            }
            set_post_string(curl_handle_, body.c_str(), body.size());
            http_post(curl_handle_, gi);
            break;

        case HTTP_PUT:
            if (!hdr_options.size()) {
                // if no header options are set, set the content type
                set_header_options(curl_handle_, "Content-Type: application/xml");
            }
            set_put_string(curl_handle_, body.c_str(), body.size());
            http_put(curl_handle_, gi);
            break;

        case HTTP_DELETE:
            http_delete(curl_handle_, gi);
            break;

        default:
            assert(0);
    }
}

void HttpConnection::AssignData(const char *ptr, size_t size) {

    buf_.assign(ptr, size);

    // callback to client
    boost::system::error_code error;
    if (cb_ != NULL)
        cb_(buf_, error);
}

const std::string &HttpConnection::GetData() {
    return buf_;
}

void HttpConnection::UpdateOffset(size_t bytes) {
    offset_ += bytes;
}

size_t HttpConnection::GetOffset() {
    return offset_;
}

HttpClient::HttpClient(EventManager *evm) : 
  TcpServer(evm) , 
  curl_timer_(TimerManager::CreateTimer(*evm->io_service(), "http client",
              TaskScheduler::GetInstance()->GetTaskId("http client"), 0)),
  id_(0), work_queue_(TaskScheduler::GetInstance()->GetTaskId("http client"), 0,
              boost::bind(&HttpClient::DequeueEvent, this, _1)) { 
    gi_ = (struct _GlobalInfo *)malloc(sizeof(struct _GlobalInfo));
    memset(gi_, 0, sizeof(struct _GlobalInfo));
}

void HttpClient::ShutdownInternal() {

    for (HttpConnectionMap::iterator iter = map_.begin(), next = iter;
         iter != map_.end(); iter = next) {
        next++;
        RemoveConnectionInternal(iter->second);
    }

    curl_multi_cleanup(gi_->multi);
    TimerManager::DeleteTimer(curl_timer_);
    SessionShutdown();
    
    assert(!map_.size());
}

void HttpClient::Shutdown() {
    work_queue_.Enqueue(boost::bind(&HttpClient::ShutdownInternal, 
                        this));
}

HttpClient::~HttpClient() {
    free(gi_);
}

void HttpClient::Init() {
    curl_init(this);
}

void HttpClient::SessionShutdown() {
    TcpServer::Shutdown();
}

boost::asio::io_service *HttpClient::io_service() {
    return this->event_manager()->io_service();
};

TcpSession *HttpClient::AllocSession(Socket *socket) {
    HttpClientSession *session = new HttpClientSession(this, socket);
    return session;
}

TcpSession *HttpClient::CreateSession() {
    TcpSession *session = TcpServer::CreateSession();
    Socket *socket = session->socket();
    boost::system::error_code err;
    socket->open(boost::asio::ip::tcp::v4(), err);

    if (err) {
        LOG(ERROR, "http socket open failed: " << err);
        return NULL;
    }

    err = session->SetSocketOptions();
    return session;
}

HttpConnection *HttpClient::CreateConnection(boost::asio::ip::tcp::endpoint ep) {
    HttpConnection *conn = new HttpConnection(ep, ++id_, this);
    return conn;
}

bool HttpClient::AddConnection(HttpConnection *conn) {
    Key key = std::make_pair(conn->endpoint(), conn->id());
    if (map_.find(key) == map_.end()) {
        map_.insert(key, conn);
        return true;
    }
    return false;
}

void HttpClient::RemoveConnection(HttpConnection *connection) {
    connection->ClearCallback();
    work_queue_.Enqueue(boost::bind(&HttpClient::RemoveConnectionInternal, 
                                     this, connection));
}

void HttpClient::ProcessEvent(EnqueuedCb cb) {
    work_queue_.Enqueue(cb);
}

void HttpClient::TimerErrorHandler(std::string name, std::string error) {
}

bool HttpClient::TimerCb() {
    return timer_cb(gi_);
}

void HttpClient::StartTimer(long timeout_ms) {
    CancelTimer();
    curl_timer_->Start(timeout_ms, boost::bind(&HttpClient::TimerCb, this)); 
}

void HttpClient::CancelTimer() {
    curl_timer_->Cancel();
}

bool HttpClient::IsErrorHard(const boost::system::error_code &ec) {
    return TcpSession::IsSocketErrorHard(ec);
}

void HttpClient::RemoveConnectionInternal(HttpConnection *connection) {
    boost::asio::ip::tcp::endpoint endpoint = connection->endpoint();
    size_t id = connection->id();
    del_conn(connection, gi_);
    map_.erase(std::make_pair(endpoint, id));
    return;
}

bool HttpClient::DequeueEvent(EnqueuedCb cb) {
    cb();
    return true;
}
