/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/bind.hpp>
#include <boost/function.hpp>
#include "base/util.h"
#include "base/test/task_test_util.h"
#include <iomanip>
#include <ctype.h>
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <stdlib.h>
#include <rapidjson/stringbuffer.h>
#include <boost/algorithm/string.hpp>
#include "keystone-client.h"

#include "vncapi.h"

RespBlock::RespBlock(HttpConnection *c, std::string uri,
            boost::function<void(rapidjson::Document&,
                boost::system::error_code&, std::string, int, std::string,
                std::map<std::string, std::string>*)> cb):
        conn_(c), uri_(uri), cb_(cb)
{
}

HttpConnection *
RespBlock::GetConnection()
{
    return conn_;
}

void
RespBlock::AddBody(std::string s)
{
    body_ += s;
}

void
RespBlock::Clear(HttpConnection *c)
{
    body_.clear();
    if (c)
        conn_ = c;
}

std::string
RespBlock::GetBody()
{
    return body_;
}

std::string
RespBlock::GetUri()
{
    return uri_;
}

void
RespBlock::ShowDetails()
{
    std::cout << conn_->Version() << "->" 
        << conn_->Reason() << "(" << conn_->Status() << ")\n";
}

boost::function<void(rapidjson::Document&, boost::system::error_code&,
        std::string, int, std::string, std::map<std::string, std::string>*)>
RespBlock::GetCallBack()
{
    return cb_;
}


#define CHR_PER_LN 16
void
VncApi::hex_dump(std::string s)
{
#define __DEBUG__ 1
#ifdef __DEBUG__
    unsigned long a = 0;
    int rem = s.length();
    const char *p = s.c_str();
    while (rem > 0) {
        std::cout << std::setfill('0') << std::setw(8) << std::hex << a;
        for (int i = 0; i < CHR_PER_LN; i++) {
            if( i % 8 == 0 ) std::cout << ' ';
            if ( i < rem )
                std::cout << ' ' << std::setfill('0') << std::setw(2) <<
                    std::hex << (unsigned) p[i];
            else
                std::cout << "   ";
        }
        std::cout << "   ";
        for (int i = 0; i < rem && i < CHR_PER_LN; i++)
            if (isprint(p[i]))
                std::cout << p[i];
            else
                std::cout << '.';
        std::cout << '\n';
        a += CHR_PER_LN;
        p += CHR_PER_LN;
        rem -= CHR_PER_LN;
    }
    std::cout << std::dec;
#endif
}

std::string
VncApi::GetTokenFromKeystone()
{
    keystone_context_t c = {};
    std::string token;
    if (keystone_global_init() == KSERR_SUCCESS) {
        if (keystone_start(&c) == KSERR_SUCCESS) {
            std::ostringstream url;
            url << cfg_->protocol << "://" << cfg_->ks_srv_ip << ":" << 
                cfg_->ks_srv_port << "/v2.0/tokens";
            if (keystone_authenticate(&c, url.str().c_str(),
                        cfg_->user.c_str(), cfg_->tenant.c_str(),
                        cfg_->password.c_str()) == KSERR_SUCCESS) {
                 token = std::string(keystone_get_auth_token(&c));
                 //hex_dump(token);
                 std::cout << "Token : " << token << std::endl;
            } else {
                std::cout << "keystone_authenticate Falied\n";
            }
            keystone_end(&c);
        } else {
            std::cout << "keystone_startFalied\n";
        }
        keystone_global_cleanup();
    } else {
        std::cout << "keystone_global_init Falied\n";
    }
    return token;
}

void
VncApi::Reauthenticate()
{
    hdr_[2] = std::string("X-AUTH-TOKEN: ") + GetTokenFromKeystone();
}

void
VncApi::Add2URI(std::ostringstream &uri, std::string &qadded, std::string key,
        std::vector<std::string> data)
{
    if (data.size() > 0) {
        uri << qadded << key << "=";
        qadded = "&";
        for (std::vector<std::string>::iterator i = data.begin();
                i != data.end(); i++) {
            uri << *i;
            if (i + 1 != data.end())
                uri << ",";
        }
    }
}

std::string
VncApi::MakeUri(std::string type, std::vector<std::string> ids,
        std::vector<std::string> filters, std::vector<std::string> parents,
        std::vector<std::string> refs, std::vector<std::string> fields)
{
    std::ostringstream uri;
    uri << type << "s";
    std::string qadded = "?";
    Add2URI(uri, qadded, "id", ids);
    Add2URI(uri, qadded, "filters", filters);
    Add2URI(uri, qadded, "parent_id", parents);
    Add2URI(uri, qadded, "refs", refs);
    Add2URI(uri, qadded, "fields", fields);
    return uri.str();
}

VncApi::VncApi(EventManager *evm, VncApiConfig *cfg) : evm_(evm), cfg_(cfg),
        client_(new HttpClient(evm_))
{
    client_->Init();
    hdr_.push_back(std::string(
                "Content-Type: application/json; charset=\"UTF-8\""));
    hdr_.push_back(std::string("X-Contrail-Useragent: a7s30:vncapi.cc"));
    //hdr_.push_back(std::string("X-AUTH-TOKEN: ") + GetTokenFromKeystone());
    hdr_.push_back(std::string("X-AUTH-TOKEN: some junk"));

    
    boost::system::error_code ec;
    api_ep_.address(boost::asio::ip::address::from_string(cfg_->cfg_srv_ip, ec));
    api_ep_.port(cfg_->cfg_srv_port);
}

void
VncApi::Stop()
{
    //TcpServerManager::DeleteServer(client_);
}

void
VncApi::GetConfig(std::string type, std::vector<std::string> ids,
        std::vector<std::string> filters, std::vector<std::string> parents,
        std::vector<std::string> refs, std::vector<std::string> fields,
        boost::function<void(rapidjson::Document&,
            boost::system::error_code &ec, std::string version, int status,
            std::string reason,
            std::map<std::string, std::string> *headers)> cb)
{
    RespBlock *rb = new RespBlock(client_->CreateConnection(api_ep_),
            MakeUri(type, ids, filters, parents, refs, fields), cb);
    rb->GetConnection()->HttpGet(rb->GetUri(), false, false, true, hdr_,
            boost::bind(&VncApi::RespHandler, this, rb, _1, _2));
}

void
VncApi::RespHandler(RespBlock *rb, std::string &str,
        boost::system::error_code &ec)
{
    hex_dump(str);
#ifdef __DEBUG__
    rb->ShowDetails();
    std::cout << "\n" << str << "\nErr: " << ec << std::endl;
#endif // __DEBUG__
    if (str == "") {
        if (rb->GetConnection()->Status() == 401) {
            // retry
            // client_->RemoveConnection(rb->GetConnection());
            // rb->Clear(client_->CreateConnection(api_ep_));
            rb->Clear();
            Reauthenticate();
            rb->GetConnection()->HttpGet(rb->GetUri(), false, false, true,
                    hdr_,
                    boost::bind(&VncApi::RespHandler, this, rb, _1, _2));
        } else {
            rapidjson::Document jdoc;
            if (rb->GetConnection()->Status() == 200)
                jdoc.Parse<0>(rb->GetBody().c_str());
            rb->GetCallBack()(jdoc, ec,
                    rb->GetConnection()->Version(),
                    rb->GetConnection()->Status(),
                    rb->GetConnection()->Reason(),
                    rb->GetConnection()->Headers());
            client_->RemoveConnection(rb->GetConnection());
            delete rb;
        }
    } else {
        rb->AddBody(str);
    }
}
