/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "http/http_session.h"

#include <map>
#include <boost/bind.hpp>
#include <cstdio>

#include "base/logging.h"
#include "base/task.h"
#include "http/http_request.h"
#include "http/http_server.h"
#include "http_parser/http_parser.h"
#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_http.h"
#include "http/http_log_types.h"

using namespace std;

int HttpSession::req_handler_task_id_ = -1;
HttpSession::map_type * HttpSession::context_map_ = 0;
tbb::mutex HttpSession::map_mutex_;
tbb::atomic<long> HttpSession::task_count_;

// Input processing context
class HttpSession::RequestBuilder {
public:
    RequestBuilder() : complete_(false) {
        parser_.data = this;
        Clear();
    }

    void Clear() {
        http_parser_init(&parser_, HTTP_REQUEST);
        request_.reset(new HttpRequest());
        complete_ = false;
        header_key_.clear();
        header_value_.clear();
    }
    size_t Parse(const u_int8_t *data, size_t datalen) {
        size_t nparsed = 
            http_parser_execute(&parser_, &settings_,
                    reinterpret_cast<const char *>(data), datalen);
        return nparsed;
    }

    bool complete() const { return complete_; }

    // Transfers ownership
    HttpRequest *GetRequest() {
        HttpRequest *request = request_.get();
        request_.release();
        return request;
    }
private:
    static int OnMessageBegin(struct http_parser *parser) {
        return 0;
    }

    static int OnHeadersComplete(struct http_parser *parser) {
        RequestBuilder *builder =
            reinterpret_cast<RequestBuilder *>(parser->data);
        builder->request_->SetMethod(static_cast<http_method>(parser->method));
        builder->request_->SetUrl(&builder->tmp_url_);
        builder->PushHeader();
        return 0;
    }

    static int OnMessageComplete(struct http_parser *parser) {
        RequestBuilder *builder =
            reinterpret_cast<RequestBuilder *>(parser->data);
        builder->complete_ = true;
        return 0;
    }

    static int OnUrl(struct http_parser *parser,
                 const char *loc, size_t length) {
        RequestBuilder *builder =
            reinterpret_cast<RequestBuilder *>(parser->data);
        builder->tmp_url_.append(loc, length);
        return 0;
    }

    static int OnStatusComplete(struct http_parser *parser) {
        return 0;
    }

    static int OnHeaderField(struct http_parser *parser,
                 const char *loc, size_t length) {
        RequestBuilder *builder =
            reinterpret_cast<RequestBuilder *>(parser->data);
        if (!builder->header_value_.empty()) {
            builder->PushHeader();
        }
        builder->header_key_.append(loc, length);
        return 0;
    }

    static int OnHeaderValue(struct http_parser *parser,
                     const char *loc, size_t length) {
        RequestBuilder *builder =
            reinterpret_cast<RequestBuilder *>(parser->data);
        builder->header_value_.append(loc, length);
        return 0;
    }

    static int OnBody(struct http_parser *parser,
              const char *loc, size_t length) {
        RequestBuilder *builder =
            reinterpret_cast<RequestBuilder *>(parser->data);
        builder->request_->SetBody(loc, length);
        return 0;
    }

    void PushHeader() {
        request_->PushHeader(header_key_, header_value_);
        header_key_.clear();
        header_value_.clear();
    }

    static struct http_parser_settings settings_;

    struct http_parser parser_;
    std::auto_ptr<HttpRequest> request_;

    bool complete_;
    string tmp_url_;        // temporary: used while parsing
    string header_key_;
    string header_value_;
};

struct http_parser_settings HttpSession::RequestBuilder::settings_ = {
    OnMessageBegin,
    OnUrl,
    OnStatusComplete,
    OnHeaderField,
    OnHeaderValue,
    OnHeadersComplete,
    OnBody,
    OnMessageComplete
};

class HttpSession::RequestHandler : public Task {
public:
    explicit RequestHandler(HttpSession *session)
    : Task(req_handler_task_id_, session->GetSessionInstance()), 
      session_(session) {
    }

    ~RequestHandler() {
        HTTP_SYS_LOG("HttpSession", SandeshLevel::UT_INFO, "RequestHandler destructor");
    }

    void NotFound(HttpSession *session, const HttpRequest *request) {
        string url = request->UrlPath().substr(1);
        static const char no_response[] =
"HTTP/1.1 404 Not Found\r\n"
"Content-Type: text/html; charset=UTF-8\r\n"
"Content-Length: 46\r\n"
"\r\n"
"<html>\n"
"<title>404 Not Found</title>\n"
"</html>\r\n"
;
        session->Send(reinterpret_cast<const u_int8_t *>(no_response),
              sizeof(no_response), NULL);
        delete request;
    }

    // Retrieve a request item from the queue. Return true if the queue
    // is empty _after_ the pop, false otherwise.
    bool FromQ(HttpRequest *& r) {
        session_->request_queue_.try_pop(r);
        return session_->request_queue_.empty();
    }
    virtual bool Run() {
        HTTP_SYS_LOG("HttpSession", SandeshLevel::UT_INFO,
                     "RequestHandler execute");
        HttpRequest *request = NULL;
        bool del_session = false;
        HttpServer *server = static_cast<HttpServer *>(session_->server());
        while (true) {
            request = NULL;
            bool empty = FromQ(request);
            if (!request) break;
            HTTP_SYS_LOG("HttpSession", SandeshLevel::UT_INFO,
                         "URL is " + request->ToString());
            if (request->ToString().empty()) {
                if (session_->event_cb_ && !session_->event_cb_.empty()) {
                    session_->event_cb_(session_.get(), request->Event());
                }
                del_session = true;
                session_->set_observer(NULL);
                session_->Close();
                delete request;
            } else {
                HttpServer::HttpHandlerFn handler =
                        server->GetHandler(request->UrlPath());
                if (handler == NULL) {
                    handler = boost::bind(&RequestHandler::NotFound,
                                          this, _1, _2);
                }
                handler(session_.get(), request);
            }

            // If the queue was empty, do not proceed further. If new request
            // items have been added to the queue, they would be processed by
            // a new task.
            if (empty) break;
        }
        if (del_session) {
            session_->set_observer(NULL);
            server->DeleteSession(session_.get());
            HTTP_SYS_LOG("HttpSession", SandeshLevel::UT_INFO, "DeleteSession");
        }
        HttpSession::task_count_--;
        return true;
    }
private:
    HttpSessionPtr session_;
};

HttpSession::HttpSession(HttpServer *server, Socket *socket)
    : TcpSession(server, socket), event_cb_(NULL) {
    if (req_handler_task_id_ == -1) {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        req_handler_task_id_ = scheduler->GetTaskId("http::RequestHandlerTask");
    }
    set_observer(boost::bind(&HttpSession::OnSessionEvent, this, _1, _2));
}

HttpSession::~HttpSession() {
    HttpRequest *request = NULL;
    while (request_queue_.try_pop(request)) {
        delete request;
    }
}

void HttpSession::AcceptSession() {
    tbb::mutex::scoped_lock lock(map_mutex_);
    context_str_ = "http%" + ToString();
    GetMap()->insert(std::make_pair(context_str_, HttpSessionPtr(this)));
    HTTP_SYS_LOG("HttpSession", SandeshLevel::UT_INFO,
        "Created Session " + context_str_);
}

void HttpSession::RegisterEventCb(SessionEventCb cb) {
    event_cb_ = cb;
}

void HttpSession::OnSessionEvent(TcpSession *session,
        enum TcpSession::Event event) {
    HttpSession *h_session = dynamic_cast<HttpSession *>(session);
    assert(h_session);

    bool was_empty = false;
    switch (event) {
    case TcpSession::CLOSE:
        {
            tbb::mutex::scoped_lock lock(map_mutex_);
            if (GetMap()->erase(h_session->context_str_)) {
                HTTP_SYS_LOG("HttpSession", SandeshLevel::UT_INFO,
                    "Removed Session " + h_session->context_str_);
            } else {
                HTTP_SYS_LOG("HttpSession", SandeshLevel::UT_INFO,
                    "Not Removed Session " + h_session->context_str_);                
            }
            lock.release();
            h_session->context_str_ = "";
            HttpRequest *request = new HttpRequest();
            string nourl = "";
            request->SetUrl(&nourl);
            request->SetEvent(event);
            was_empty = request_queue_.empty();
            request_queue_.push(request);
        }
        break;
    default:
        break;
    }

    if (was_empty) {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        RequestHandler *task = new RequestHandler(this);
        HttpSession::task_count_++;
        scheduler->Enqueue(task);
    }
}

void HttpSession::OnRead(Buffer buffer) {
    const u_int8_t *data = BufferData(buffer);
    size_t size = BufferSize(buffer);
    std::stringstream msg;
    bool was_empty = false;
    {
        msg << "HttpSession::Read " << size << " bytes";
        HTTP_SYS_LOG("HttpSession", SandeshLevel::UT_DEBUG, msg.str());

        if (context_str_.size() == 0) {
            ReleaseBuffer(buffer);
            return;
        }
        if (request_builder_.get() == NULL) {
            request_builder_.reset(new RequestBuilder());
        }
        request_builder_->Parse(data, size);
        if (request_builder_->complete()) {
            was_empty = request_queue_.empty();
            HttpRequest *request = request_builder_->GetRequest();
            HTTP_SYS_LOG("HttpSession", SandeshLevel::UT_DEBUG, request->ToString());
            request_queue_.push(request);
            request_builder_->Clear();
        }
    }
    if (was_empty) {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        RequestHandler *task = new RequestHandler(this);
        HttpSession::task_count_++;
        scheduler->Enqueue(task);
    }
    // TODO: error handling
    ReleaseBuffer(buffer);
}
