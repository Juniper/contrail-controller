/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/bind.hpp>
#include <boost/function.hpp>
#include "base/util.h"
#include "base/logging.h"
#include "base/test/task_test_util.h"
#include <iomanip>
#include <ctype.h>
#include <iostream>
#include <algorithm>
#include <sstream>
#include <unistd.h>
#include <stdlib.h>
#include <rapidjson/stringbuffer.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "vncapi.h"

RespBlock::RespBlock(HttpConnection *c, std::string uri,
            boost::function<void(contrail_rapidjson::Document&,
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

boost::function<void(contrail_rapidjson::Document&, boost::system::error_code&,
        std::string, int, std::string, std::map<std::string, std::string>*)>
RespBlock::GetCallBack()
{
    return cb_;
}


#define CHR_PER_LN 16
void
VncApi::hex_dump(std::string s)
{
//#define __DEBUG__ 1
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

void
VncApi::Reauthenticate(RespBlock *orb)
{
    if (client_) {
        HttpConnection *conn = client_->CreateConnection(ks_ep_);
        if (cfg_->ks_protocol == "https") {
            conn->set_use_ssl(true);
            conn->set_client_cert(cfg_->ks_certfile);
            conn->set_client_cert_type("PEM");
            conn->set_client_key(cfg_->ks_keyfile);
            conn->set_ca_cert(cfg_->ks_cafile);
        }
        RespBlock *rb = new RespBlock(conn, "v2.0/tokens", 0);
        std::ostringstream pstrm;
        pstrm << "{\"auth\": {\"passwordCredentials\": {\"username\": \"" <<
            cfg_->ks_user << "\", \"password\": \"" << cfg_->ks_password <<
            "\"}, \"tenantName\": \"" << cfg_->ks_tenant << "\"}}";
        rb->GetConnection()->HttpPost(pstrm.str(), rb->GetUri(), false, false,
                true, kshdr_, boost::bind(&VncApi::KsRespHandler,
                shared_from_this(), rb, orb, _1, _2));
    }
}

bool
VncApi::CondTest(std::string s)
//VncApi::CondTest(std::string s)
{
    return boost::starts_with(s, "X-AUTH-TOKEN:");
}

std::string VncApi::GetToken(RespBlock *rb) const {
    contrail_rapidjson::Document jdoc;
    jdoc.Parse<0>(rb->GetBody().c_str());

    if (!jdoc.IsObject() || !jdoc.HasMember("access"))
        return "";

    if (!jdoc["access"].IsObject() || !jdoc["access"].HasMember("token"))
        return "";

    if (!jdoc["access"]["token"].IsObject())
        return "";

    if (!jdoc["access"]["token"].HasMember("id"))
        return "";

    if (!jdoc["access"]["token"]["id"].IsString())
        return "";

    return jdoc["access"]["token"]["id"].GetString();
}

void
VncApi::KsRespHandler(RespBlock *rb, RespBlock *orb, std::string &str,
        boost::system::error_code &ec)
{
    if (client_) {
        if (str == "") {
            if (rb->GetConnection()->Status() == 200) {
                contrail_rapidjson::Document jdoc;
                jdoc.Parse<0>(rb->GetBody().c_str());

                std::string token = GetToken(rb);
                if (!token.empty()) {
                    hdr_.erase(std::remove_if(hdr_.begin(), hdr_.end(),
                        boost::bind(&VncApi::CondTest,
                            shared_from_this(), _1)), hdr_.end());
                    hdr_.push_back(std::string("X-AUTH-TOKEN: ") + token);
                    orb->GetConnection()->HttpGet(orb->GetUri(), false,
                            false, true, hdr_, boost::bind(
                                &VncApi::RespHandler,
                            shared_from_this(), orb, _1, _2));
                    client_->RemoveConnection(rb->GetConnection());
                    delete rb;
                    return;
                }
            }
            client_->RemoveConnection(orb->GetConnection());
            delete orb;
            client_->RemoveConnection(rb->GetConnection());
            delete rb;
        } else {
            rb->AddBody(str);
        }
    } else {
        delete orb;
        delete rb;
    }
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
        client_(new HttpClient(evm_, "vnc-api http client"))
{
    client_->Init();
    hdr_.push_back(std::string(
                "Content-Type: application/json; charset=\"UTF-8\""));
    hdr_.push_back(std::string("X-Contrail-Useragent: a7s30:vncapi.cc"));
    //hdr_.push_back(std::string("X-AUTH-TOKEN: ") + GetTokenFromKeystone());
    //hdr_.push_back(std::string("X-AUTH-TOKEN: some junk"));
    kshdr_ = hdr_;


    boost::system::error_code ec;
    SetApiServerAddress();
    ks_ep_.address(boost::asio::ip::address::from_string(cfg_->ks_srv_ip, ec));
    ks_ep_.port(cfg_->ks_srv_port);
    if (cfg_->api_use_ssl) {
        boost::property_tree::ptree pt;
        try {
            boost::property_tree::ini_parser::read_ini(
                "/etc/contrail/vnc_api_lib.ini", pt);
        } catch (const boost::property_tree::ptree_error &e) {
            LOG(ERROR, "Failed to parse /etc/contrail/vnc_api_lib.ini : " <<
                e.what());
            exit(1);
        }
        cfg_->api_keyfile = pt.get<std::string>("global.keyfile", "");
        cfg_->api_certfile = pt.get<std::string>("global.certfile", "");
        cfg_->api_cafile = pt.get<std::string>("global.cafile", "");
    }
}

void
VncApi::SetApiServerAddress() {
    boost::system::error_code ec;
    api_ep_.address(boost::asio::ip::address::from_string(cfg_->api_srv_ip,
                ec));
    api_ep_.port(cfg_->api_srv_port);
}

void
VncApi::Stop()
{
    std::cout  << "VncApi::Stop\n";
    TcpServerManager::DeleteServer(client_);
    client_ = 0;
    {
        hdr_.clear();
        kshdr_.clear();
    }
}

void
VncApi::GetConfig(std::string type, std::vector<std::string> ids,
        std::vector<std::string> filters, std::vector<std::string> parents,
        std::vector<std::string> refs, std::vector<std::string> fields,
        boost::function<void(contrail_rapidjson::Document&,
            boost::system::error_code &ec, std::string version, int status,
            std::string reason,
            std::map<std::string, std::string> *headers)> cb)
{
    if (client_) {
        HttpConnection *conn = client_->CreateConnection(api_ep_);
        if (cfg_->api_use_ssl) {
            conn->set_use_ssl(true);
            conn->set_client_cert(cfg_->api_certfile);
            conn->set_client_cert_type("PEM");
            conn->set_client_key(cfg_->api_keyfile);
            conn->set_ca_cert(cfg_->api_cafile);
        }
        RespBlock *rb = new RespBlock(conn,
                MakeUri(type, ids, filters, parents, refs, fields), cb);
        rb->GetConnection()->HttpGet(rb->GetUri(), false, false, true, hdr_,
                boost::bind(&VncApi::RespHandler, shared_from_this(),
                        rb, _1, _2));
    }
}

void
VncApi::RespHandler(RespBlock *rb, std::string &str,
        boost::system::error_code &ec)
{
#ifdef __DEBUG__
    hex_dump(str);
    rb->ShowDetails();
    std::cout << "\n" << str << "\nErr: " << ec << std::endl;
#endif // __DEBUG__
    if (client_) {
        if (str == "") {
            if (rb->GetConnection()->Status() == 401) {
                // retry
                // client_->RemoveConnection(rb->GetConnection());
                // rb->Clear(client_->CreateConnection(api_ep_));
                rb->Clear();
                Reauthenticate(rb);
            } else {
                contrail_rapidjson::Document jdoc;
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
    } else {
        delete rb;
    }
}
