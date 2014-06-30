/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/bind.hpp>
#include <boost/asio/ip/host_name.hpp>
#include <boost/foreach.hpp>
#include <boost/tokenizer.hpp>
#include <boost/assign/list_of.hpp>

#include <isc/hmacmd5.h>
#include <isc/hmacsha.h>

#include "base/contrail_ports.h"
#include "http/http_request.h"
#include "http/http_session.h"
#include "http/http_server.h"
#include "http/client/http_client.h"
#include "http/client/http_curl.h"
#include "io/event_manager.h"
#include "cmn/agent_cmn.h"
#include "oper/operdb_init.h"
#include "oper/mirror_table.h"
#include "oper/interface_common.h"
#include "oper/global_vrouter.h"
#include "pkt/pkt_handler.h"
#include "services/services_types.h"
#include "services/services_init.h"
#include "services/metadata_proxy.h"
#include "services/services_sandesh.h"

////////////////////////////////////////////////////////////////////////////////

#define METADATA_TRACE(obj, arg)                                               \
do {                                                                           \
    std::ostringstream _str;                                                   \
    _str << arg;                                                               \
    Metadata##obj::TraceMsg(MetadataTraceBuf, __FILE__, __LINE__, _str.str()); \
} while (false)                                                                \

std::map<uint16_t, std::string>
        g_http_error_map = boost::assign::map_list_of<uint16_t, std::string>
                                        (404, "404 Not Found")
                                        (500, "500 Internal Server Error")
                                        (501, "501 Not Implemented")
                                        (502, "502 Bad Gateway")
                                        (503, "503 Service Unavailable")
                                        (504, "504 Gateway Timeout");

static std::string ErrorMessage(uint16_t ec) {
    std::map<uint16_t, std::string>::iterator iter = g_http_error_map.find(ec);
    if (iter == g_http_error_map.end())
        return ""; 
    return iter->second;
}

// Get HMAC SHA256 digest
static std::string
GetHmacSha256(const std::string &key, const std::string &data) {
    isc_hmacsha256_t hmacsha256;
    isc_hmacsha256_init(&hmacsha256, (const unsigned char *)key.c_str(),
                        key.length());
    isc_hmacsha256_update(&hmacsha256, (const isc_uint8_t *)data.c_str(),
                          data.length());
    unsigned char hmac_sha256_digest[ISC_SHA512_DIGESTLENGTH];
    isc_hmacsha256_sign(&hmacsha256, hmac_sha256_digest,
                        ISC_SHA256_DIGESTLENGTH);
    std::stringstream str;
    for (unsigned int i = 0; i < ISC_SHA256_DIGESTLENGTH; i++) {
        str << std::hex << std::setfill('0') << std::setw(2)
            << (int) hmac_sha256_digest[i];
    }
    return str.str();
}

////////////////////////////////////////////////////////////////////////////////

MetadataProxy::MetadataProxy(ServicesModule *module,
                             const std::string &secret)
    : services_(module), shared_secret_(secret),
      http_server_(new HttpServer(services_->agent()->event_manager())),
      http_client_(new HttpClient(services_->agent()->event_manager())) {

    // Register wildcard entry to match any URL coming on the metadata port
    http_server_->RegisterHandler(HTTP_WILDCARD_ENTRY,
        boost::bind(&MetadataProxy::HandleMetadataRequest, this, _1, _2));
    http_server_->Initialize(0);
    services_->agent()->set_metadata_server_port(http_server_->GetPort());

    http_client_->Init();
}

MetadataProxy::~MetadataProxy() {
}

void MetadataProxy::CloseSessions() {
    for (SessionMap::iterator it = metadata_sessions_.begin();
         it != metadata_sessions_.end(); ) {
        SessionMap::iterator next = ++it;
        CloseClientSession(it->second.conn);
        CloseServerSession(it->first);
        it = next;
    }

    assert(metadata_sessions_.empty());
    assert(metadata_proxy_sessions_.empty());
}

void
MetadataProxy::Shutdown() {
    http_server_->Shutdown();
    http_server_ = NULL;
    http_client_->Shutdown();
    http_client_ = NULL;
}

void 
MetadataProxy::HandleMetadataRequest(HttpSession *session, const HttpRequest *request) {
    bool conn_close = false;
    std::vector<std::string> header_options;
    std::string vm_ip, vm_uuid, vm_project_uuid;
    metadata_stats_.requests++;
    boost::asio::ip::address_v4 ip = session->remote_endpoint().address().to_v4();

    if (!services_->agent()->interface_table()->
         FindVmUuidFromMetadataIp(ip, &vm_ip, &vm_uuid, &vm_project_uuid)) {
        METADATA_TRACE(Trace, "Error: Interface Config not available; "
                       << "; Request for VM : " << ip);
        ErrorClose(session, 500);
        http_server_->DeleteSession(session);
        delete request;
        return;
    }
    std::string signature = GetHmacSha256(shared_secret_, vm_uuid);
    const HttpRequest::HeaderMap &req_header = request->Headers();
    for (HttpRequest::HeaderMap::const_iterator it = req_header.begin();
         it != req_header.end(); ++it) {
        std::string option = boost::to_lower_copy(it->first);
        if (option == "host") {
            continue;
        }
        if (option == "connection") {
            std::string val = boost::to_lower_copy(it->second);
            if (val == "close")
                conn_close = true;
            continue;
        }
        header_options.push_back(std::string(it->first + ": " + it->second));
    }

    // keystone uses uuids without dashes and that is what ends up in
    // the nova database entry for the instance. Remove dashes from the
    // uuid string representation.
    boost::replace_all(vm_project_uuid, "-", "");
    header_options.push_back(std::string("X-Forwarded-For: " + vm_ip));
    header_options.push_back(std::string("X-Instance-ID: " + vm_uuid));
    header_options.push_back(std::string("X-Tenant-ID: " + vm_project_uuid));
    header_options.push_back(std::string("X-Instance-ID-Signature: " +
                                         signature));

    std::string uri = request->UrlPath().substr(1); // ignore the first "/"
    const std::string &body = request->Body();
    {
        tbb::mutex::scoped_lock lock(mutex_);
        HttpConnection *conn = GetProxyConnection(session, conn_close);
        if (conn) {
            switch(request->GetMethod()) {
                case HTTP_GET: {
                    conn->HttpGet(uri, true, false, header_options,
                    boost::bind(&MetadataProxy::HandleMetadataResponse,
                                this, conn, HttpSessionPtr(session), _1, _2));
                    METADATA_TRACE(Trace, "GET request for VM : " << vm_ip
                                   << " URL : " << uri);
                    break;
                }

                case HTTP_HEAD: {
                    conn->HttpHead(uri, true, false, header_options,
                    boost::bind(&MetadataProxy::HandleMetadataResponse,
                                this, conn, HttpSessionPtr(session), _1, _2));
                    METADATA_TRACE(Trace, "HEAD request for VM : " << vm_ip
                                   << " URL : " << uri);
                    break;
                }

                case HTTP_POST: {
                    conn->HttpPost(body, uri, true, false, header_options,
                    boost::bind(&MetadataProxy::HandleMetadataResponse,
                                this, conn, HttpSessionPtr(session), _1, _2));
                    METADATA_TRACE(Trace, "POST request for VM : " << vm_ip
                                   << " URL : " << uri);
                    break;
                }

                case HTTP_PUT: {
                    conn->HttpPut(body, uri, true, false, header_options,
                    boost::bind(&MetadataProxy::HandleMetadataResponse,
                                this, conn, HttpSessionPtr(session), _1, _2));
                    METADATA_TRACE(Trace, "PUT request for VM : " << vm_ip
                                   << " URL : " << uri);
                    break;
                }

                case HTTP_DELETE: {
                    conn->HttpDelete(uri, true, false, header_options,
                    boost::bind(&MetadataProxy::HandleMetadataResponse,
                                this, conn, HttpSessionPtr(session), _1, _2));
                    METADATA_TRACE(Trace, "Delete request for VM : " << vm_ip
                                   << " URL : " << uri);
                    break;
                }

                default:
                    METADATA_TRACE(Trace, "Error: Unsupported Method; "
                                   << "Request Method: " << request->GetMethod()
                                   << "; Request for VM: " << vm_ip);
                    CloseClientSession(conn);
                    ErrorClose(session, 501);
                    http_server_->DeleteSession(session);
                    break;
            }
        } else {
            METADATA_TRACE(Trace, "Error: Config not available; "
                           << "Request Method: " << request->GetMethod()
                           << "; Request for VM : " << vm_ip);
            ErrorClose(session, 500);
            http_server_->DeleteSession(session);
        }
    }

    delete request;
}

// Metadata Response from Nova API service
void
MetadataProxy::HandleMetadataResponse(HttpConnection *conn, HttpSessionPtr session,
                                      std::string &msg, boost::system::error_code &ec) {
    bool delete_session = false;
    {
        tbb::mutex::scoped_lock lock(mutex_);

        // Ignore if session is closed in the meantime
        SessionMap::iterator it = metadata_sessions_.find(session.get());
        if (it == metadata_sessions_.end())
            return;

        std::string vm_ip, vm_uuid, vm_project_uuid;
        boost::asio::ip::address_v4 ip = session->remote_endpoint().address().to_v4();
        services_->agent()->interface_table()->
            FindVmUuidFromMetadataIp(ip, &vm_ip, &vm_uuid, &vm_project_uuid);

        if (!ec) {
            METADATA_TRACE(Trace, "Metadata for VM : " << vm_ip << " Response : " << msg);
            session->Send(reinterpret_cast<const u_int8_t *>(msg.c_str()),
                          msg.length(), NULL);
        } else {
            METADATA_TRACE(Trace, "Metadata for VM : " << vm_ip << " Error : " << 
                                  boost::system::system_error(ec).what());
            CloseClientSession(conn);
            ErrorClose(session.get(), 502);
            delete_session = true;
            goto done;
        }

        if (!ec && it->second.close_req) {
            std::stringstream str(msg);
            std::string option;
            str >> option;
            if (option == "Content-Length:") {
                str >> it->second.content_len;
            } else if (msg == "\r\n") {
                it->second.header_end = true;
                if (it->second.header_end && !it->second.content_len) {
                    CloseClientSession(it->second.conn);
                    CloseServerSession(session.get());
                    delete_session = true;
                    metadata_stats_.responses++;
                }
            } else if (it->second.header_end) {
                it->second.data_sent += msg.length();
                if (it->second.data_sent >= it->second.content_len) {
                    CloseClientSession(it->second.conn);
                    CloseServerSession(session.get());
                    delete_session = true;
                    metadata_stats_.responses++;
                }
            }
        }
    }

done:
    if (delete_session) {
        http_server_->DeleteSession(session.get());
    }
}

void
MetadataProxy::OnServerSessionEvent(HttpSession *session, TcpSession::Event event) {
    switch (event) {
        case TcpSession::CLOSE: {
            tbb::mutex::scoped_lock lock(mutex_);
            SessionMap::iterator it = metadata_sessions_.find(session);
            if (it == metadata_sessions_.end())
                break;
            CloseClientSession(it->second.conn);
            metadata_sessions_.erase(it);
            break;
        }

        default:
            break;
    }
}

void
MetadataProxy::OnClientSessionEvent(HttpClientSession *session, TcpSession::Event event) {
    switch (event) {
        case TcpSession::CLOSE: {
            {
                tbb::mutex::scoped_lock lock(mutex_);
                ConnectionSessionMap::iterator it = 
                    metadata_proxy_sessions_.find(session->Connection());
                if (it == metadata_proxy_sessions_.end())
                    break;
                CloseServerSession(it->second);
                CloseClientSession(session->Connection());
            }
            http_server_->DeleteSession(session);
            break;
        }

        default:
            break;
    }
}

HttpConnection *
MetadataProxy::GetProxyConnection(HttpSession *session, bool conn_close) {
    SessionMap::iterator it = metadata_sessions_.find(session);
    if (it != metadata_sessions_.end()) {
        it->second.close_req = conn_close;
        return it->second.conn;
    }

    uint16_t nova_port, linklocal_port;
    Ip4Address nova_server, linklocal_server;
    if (!services_->agent()->oper_db()->global_vrouter()->FindLinkLocalService(
        GlobalVrouter::kMetadataService, &linklocal_server, &linklocal_port,
        &nova_server, &nova_port))
        return NULL;

    boost::asio::ip::tcp::endpoint http_ep;
    http_ep.address(nova_server);
    http_ep.port(nova_port);

    HttpConnection *conn = http_client_->CreateConnection(http_ep);
    conn->RegisterEventCb(
             boost::bind(&MetadataProxy::OnClientSessionEvent, this, _1, _2));
    session->RegisterEventCb(
             boost::bind(&MetadataProxy::OnServerSessionEvent, this, _1, _2));
    SessionData data(conn, conn_close);
    metadata_sessions_.insert(SessionPair(session, data));
    metadata_proxy_sessions_.insert(ConnectionSessionPair(conn, session));
    metadata_stats_.proxy_sessions++;
    return conn;
}

void
MetadataProxy::CloseServerSession(HttpSession *session) {
    session->Close();
    metadata_sessions_.erase(session);
}

void
MetadataProxy::CloseClientSession(HttpConnection *conn) {
    HttpClient *client = conn->client();
    client->RemoveConnection(conn);
    metadata_proxy_sessions_.erase(conn);
}

void
MetadataProxy::ErrorClose(HttpSession *session, uint16_t error) {
    std::string message = ErrorMessage(error);
    char body[512];
    snprintf(body, sizeof(body), "<html>\n"
                                 "<head>\n"
                                 " <title>%s</title>\n"
                                 "</head>\n"
                                 "</html>\n", message.c_str());
    char response[512];
    snprintf(response, sizeof(response),
             "HTTP/1.1 %s\n"
             "Content-Type: text/html; charset=UTF-8\n"
             "Content-Length: %u\n"
             "\n%s", message.c_str(), (unsigned int) strlen(body), body);
    session->Send(reinterpret_cast<const u_int8_t *>(response),
                  strlen(response), NULL);
    CloseServerSession(session);
    metadata_stats_.internal_errors++;
}
