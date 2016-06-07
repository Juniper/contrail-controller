/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef _ROOT_REST_SERVER_H_
#define _ROOT_REST_SERVER_H_

#include <boost/regex.hpp>

class HttpServer;
class HttpSession;
class HttpRequest;
class Agent;

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

    void VmPortDeleteHandler(const struct RESTData&);
    void VmPortGetHandler(const struct RESTData&);
    void VmPortPostHandler(const struct RESTData&);
    void VmPortSyncHandler(const struct RESTData& data);

 private:
    Agent *agent_;
    HttpServer *http_server_;
    DISALLOW_COPY_AND_ASSIGN(RESTServer);
};

#endif  // _ROOT_REST_SERVER_H_
