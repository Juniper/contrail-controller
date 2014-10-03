/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __HTTP_SESSION_H__
#define __HTTP_SESSION_H__

#include <boost/scoped_ptr.hpp>
#include <tbb/concurrent_queue.h>
#include <tbb/atomic.h>

#include "base/util.h"
#include "io/tcp_session.h"

class HttpRequest;
class HttpServer;

class HttpSession: public TcpSession {
  public:
    typedef boost::function<void(HttpSession *session,
                                 enum TcpSession::Event event)> SessionEventCb;

    explicit HttpSession(HttpServer *server, Socket *socket);
    virtual ~HttpSession();
    const std::string get_context() { return context_str_; }

    static bool SendSession(std::string const& s,
            const u_int8_t *data, size_t size, size_t *sent) {
        HttpSessionPtr hs = GetSession(s);
        if (!hs) return false;
        return hs->Send(data, size, sent);
    }
    static std::string get_client_context(std::string const& s) {
        HttpSessionPtr hs = GetSession(s);
        if (!hs) return "";
        return hs->get_client_context();
    }
    static bool set_client_context(std::string const& s,
            const std::string& ctx) {
        HttpSessionPtr hs = GetSession(s);
        if (!hs) return false;
        hs->set_client_context(ctx);
        return true;
    }    
    static tbb::atomic<long> GetPendingTaskCount() {
        return task_count_;
    }

    void AcceptSession();
    void RegisterEventCb(SessionEventCb cb);

  protected:
    virtual void OnRead(Buffer buffer);

  private:
    class RequestBuilder;
    class RequestHandler;
    typedef boost::intrusive_ptr<HttpSession> HttpSessionPtr;
    typedef std::map<std::string, HttpSessionPtr> map_type;

    void OnSessionEvent(TcpSession *session,
            enum TcpSession::Event event);

    static map_type* GetMap() {
        if (!context_map_) {
            context_map_ = new map_type();
        }
        return context_map_;
    }
    static HttpSessionPtr GetSession(std::string const& s) {
        tbb::mutex::scoped_lock lock(map_mutex_);
        map_type::iterator it = GetMap()->find(s);
        if (it == GetMap()->end()) {
            return 0;
        }
        return it->second;
    }
    const std::string get_client_context() { return client_context_str_; }
    void set_client_context(const std::string& client_ctx)
      { client_context_str_ = client_ctx; }
    boost::scoped_ptr<RequestBuilder> request_builder_;
    tbb::concurrent_queue<HttpRequest *> request_queue_;
    std::string context_str_;
    std::string client_context_str_;
    SessionEventCb event_cb_;

    static int req_handler_task_id_;
    static map_type* context_map_;
    static tbb::mutex map_mutex_;
    static tbb::atomic<long> task_count_;

    DISALLOW_COPY_AND_ASSIGN(HttpSession);
};

#endif /* __HTTP_SESSION_H__ */
