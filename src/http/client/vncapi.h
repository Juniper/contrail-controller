/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __VNC_API_H__
#define __VNC_API_H__

#include "http_client.h"
#include "http_curl.h"
#include "io/event_manager.h"
#include <string>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <boost/algorithm/string.hpp>
#include <boost/enable_shared_from_this.hpp>

struct VncApiConfig {
    std::string  api_srv_ip;
    int          api_srv_port;
    bool         api_use_ssl;
    std::string  api_keyfile;
    std::string  api_certfile;
    std::string  api_cafile;
    std::string  ks_srv_ip;
    int          ks_srv_port;
    std::string  ks_protocol;
    std::string  ks_user;
    std::string  ks_password;
    std::string  ks_tenant;
    std::string  ks_keyfile;
    std::string  ks_certfile;
    std::string  ks_cafile;
};

class RespBlock {
    public:
    RespBlock(HttpConnection *c, std::string uri,
            boost::function<void(contrail_rapidjson::Document&,
                boost::system::error_code&, std::string, int, std::string,
                std::map<std::string, std::string>*)> cb);
    HttpConnection *GetConnection();
    void AddBody(std::string s);
    void Clear(HttpConnection *c=0);
    std::string GetBody();
    std::string GetUri();
    void ShowDetails();
    boost::function<void(contrail_rapidjson::Document&,
            boost::system::error_code&, std::string, int, std::string,
            std::map<std::string, std::string>*)> GetCallBack();
    private:
    HttpConnection* conn_;
    std::string uri_;
    boost::function<void(contrail_rapidjson::Document&,
            boost::system::error_code&, std::string, int, std::string,
            std::map<std::string, std::string>*)> cb_;
    std::string body_;
};


class VncApi : public boost::enable_shared_from_this<VncApi> {
    private:
    EventManager *evm_;
    VncApiConfig *cfg_;
    HttpClient* client_;
    std::vector<std::string> hdr_;
    std::vector<std::string> kshdr_;
    boost::asio::ip::tcp::endpoint api_ep_;
    boost::asio::ip::tcp::endpoint ks_ep_;

    void hex_dump(std::string s);
    void Reauthenticate(RespBlock *orb);
    void Add2URI(std::ostringstream &uri, std::string &qadded, std::string key,
            std::vector<std::string> data);
    std::string MakeUri(std::string type, std::vector<std::string> ids,
            std::vector<std::string> filters, std::vector<std::string> parents,
            std::vector<std::string> refs, std::vector<std::string> fields);
    void KsRespHandler(RespBlock *rb, RespBlock *orb, std::string &str,
            boost::system::error_code &ec);
    bool CondTest(std::string s);
    public:
    VncApi(EventManager *evm, VncApiConfig *cfg);
    virtual ~VncApi() { Stop(); }
    void Stop();
    void SetApiServerAddress();
    void GetConfig(std::string type, std::vector<std::string> ids,
            std::vector<std::string> filters, std::vector<std::string> parents,
            std::vector<std::string> refs, std::vector<std::string> fields,
            boost::function<void(contrail_rapidjson::Document&,
                boost::system::error_code &ec, std::string version, int status,
                std::string reason,
                std::map<std::string, std::string> *headers)> cb);
    void RespHandler(RespBlock *rb, std::string &str,
            boost::system::error_code &ec);
    std::string GetToken(RespBlock *rb) const;
};
#endif
