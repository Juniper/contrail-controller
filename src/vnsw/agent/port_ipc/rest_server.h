/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef _ROOT_REST_SERVER_H_
#define _ROOT_REST_SERVER_H_

#include <map>
#include <boost/asio/ip/address.hpp>
#include <boost/regex.hpp>

#include <http/http_server.h>
#include <http/http_request.h>
#include <http/http_session.h>
#include <cmn/agent.h>

class RESTServer {
 public:
    explicit RESTServer(Agent *agent);
    virtual ~RESTServer();

    void HandleRequest(HttpSession* session, const HttpRequest* request);
    void Shutdown();

 protected:
    // REST handlers
    struct RESTData {
        const boost::smatch* match;
        enum http_method method;
        HttpSession* session;
        const HttpRequest* request;
    };

    class HandlerSpecifier {
     public:
        typedef void (RESTServer::*HandlerFunc)(const struct RESTData&);

        HandlerSpecifier(const boost::regex& request_regex,
                         enum http_method method,
                         HandlerFunc handler_func)
            : request_regex(request_regex),
              method(method),
              handler_func(handler_func) {}

        boost::regex request_regex;
        enum http_method method;
        HandlerFunc handler_func;
    };

    static const std::vector<HandlerSpecifier> RESTHandlers_;

    void VmPortPutHandler(const struct RESTData&);
    void VmPortDeleteHandler(const struct RESTData&);
    void VmPortGetHandler(const struct RESTData&);
    void VmPortPostHandler(const struct RESTData&);

 private:
    Agent *agent_;
    HttpServer *http_server_;
    tbb::mutex mutex_;
    DISALLOW_COPY_AND_ASSIGN(RESTServer);
};

#endif  // _ROOT_REST_SERVER_H_
