/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "http_client.h"
#include <boost/bind.hpp>
#include "http_curl.h"
#include "base/util.h"
#include "base/test/task_test_util.h"
#include "io/event_manager.h"

EventManager evm;
int num;
void httpc_print(HttpConnection *conn, std::string &str, 
        boost::system::error_code &ec) {
    num++;

    std::cout << str << std::endl;
    std::cout << "------ Done -----" << std::endl;
   
    HttpClient *client = conn->client();
    client->RemoveConnection(conn);

    if (num  == 2) {
        task_util::WaitForIdle();
        TcpServerManager::DeleteServer(client);
        evm.Shutdown();
        exit(0);
    }
}

int main() {
    HttpClient *client = new HttpClient(&evm);
    client->Init();

    boost::asio::ip::tcp::endpoint ep;
    boost::system::error_code ec;
    std::string ip("10.84.7.1");
    ep.address(boost::asio::ip::address::from_string(ip, ec));
    ep.port(5998);

    HttpConnection *conn = client->CreateConnection(ep);

    std::string putstr("<ifmap-server><uuid> 57 </uuid> </ifmap-server>");
    std::string uri("publish");
    conn->HttpPost(putstr, uri, boost::bind(&httpc_print, conn, _1, _2));

    conn = client->CreateConnection(ep);
    std::string putstr2("<ifmap-server><uuid> 59 </uuid> </ifmap-server>");
    conn->HttpPost(putstr2, uri, boost::bind(&httpc_print, conn, _1, _2));

    // std::string uri("");
    // conn->HttpGet(uri, boost::bind(&httpc_print, conn, _1, _2));
    evm.Run();
    return 0;
}
