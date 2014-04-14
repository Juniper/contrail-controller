/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <iostream>
#include <istream>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <async/TAsioAsync.h>
#include <protocol/TBinaryProtocol.h>

#include "io/event_manager.h"
#include "gen-cpp/InstanceService.h"

char macs[10][64];
using namespace apache::thrift;

class Agent;
void RouterIdDepInit(Agent *agent) {
}

void AddPortCallback(tuuid port_id, std::string tap_name, bool ret) 
{
    std::cout << "Device " << tap_name << ", Return value " << ret << std::endl;
}

void AddPortErrback(const InstanceService_AddPort_result& result) 
{
    std::cout << "Exception caugght " << __FUNCTION__ << std::endl;
}

void DeletePortCallback(tuuid port_id, bool ret)
{
    std::cout << "DeletePort " << ret << std::endl;
}

void DeletePortErrback(const InstanceService_DeletePort_result& result) 
{
    std::cout << "Exception caugght " << __FUNCTION__ << std::endl;
}

void AddVmPort(boost::shared_ptr<InstanceServiceAsyncClient> client, int id, char *mac) {
    std::vector<Port> pl;
    Port p;

    char name[16];
    sprintf(name, "vnet%d", id);
    p.tap_name.append(name);

    char addr[32];
    sprintf(addr, "10.10.10.%d", id);
    p.ip_address.append(addr);
    
    tuuid port_id;
    int8_t pid [] = {0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    pid[15] = id;
    port_id.insert(port_id.begin(), pid, pid + (sizeof(pid)/sizeof(int8_t)));
    p.port_id = port_id;

    tuuid instance_id;
    int8_t iid [] = {0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    iid[15] = id;
    instance_id.insert(instance_id.begin(), iid, iid + (sizeof(iid)/sizeof(int8_t)));
    p.instance_id = instance_id;

    tuuid vn_id;
    int8_t vnid [] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xf, 0xe, 0xd};
    vn_id.insert(vn_id.begin(), vnid, vnid + (sizeof(vnid)/sizeof(int8_t)));
    p.vn_id = vn_id;
    
    p.mac_address.append(mac);
    pl.push_back(p);
    client->AddPort(pl).setCallback(
        boost::bind(&AddPortCallback, p.port_id, p.tap_name, -1)).setErrback(AddPortErrback);
}

void connected(boost::shared_ptr<InstanceServiceAsyncClient> client) {
    std::cout << "connected!!!" << std::endl;

    AddVmPort(client, 1, macs[0]);
    return;
    AddVmPort(client, 2, macs[1]);

    sleep(2);

    tuuid port_id;
    int8_t pid [] = {0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    port_id.insert(port_id.begin(), pid, pid + (sizeof(pid)/sizeof(int8_t)));

    client->DeletePort(port_id).setCallback(
       boost::bind(&DeletePortCallback, port_id, -1)).setErrback(DeletePortErrback);
    client->DeletePort(port_id).setCallback(
        boost::bind(&DeletePortCallback, port_id, -1)).setErrback(DeletePortErrback);
}

int main(int argc, char* argv[])
{
    if (argc != 3) {
        std::cout << argv[0] << ": <vmport-mac1> <vmport-mac2>" << std::endl;
        return 1;
    }
    strcpy(macs[0], argv[1]);
    strcpy(macs[1], argv[2]);

    try
    {
        EventManager evm;
        
        boost::shared_ptr<protocol::TProtocolFactory> 
                protocolFactory(new protocol::TBinaryProtocolFactory());
        
        boost::shared_ptr<async::TAsioClient> client (
            new async::TAsioClient(
                *evm.io_service(),
                protocolFactory,
                protocolFactory));
        
        client->connect("localhost", 9090, connected);
        
        evm.Run();
        
        printf("Done, returning\n");
    }
    catch (std::exception& e)
    {
        std::cout << "Exception: " << e.what() << "\n";
    }
    return 0;
}
