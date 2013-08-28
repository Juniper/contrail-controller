/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap_manager.h"
#include "ifmap_channel.h"
#include "ifmap_state_machine.h"
#include "ifmap/ifmap_server.h"

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include "ifmap/ifmap_server_show_types.h"

#include <boost/function.hpp>
#include <boost/asio/io_service.hpp>

IFMapManager::IFMapManager(IFMapServer *ifmap_server, const std::string& url,
                           const std::string& user, const std::string& passwd,
                           const std::string& certstore, PollReadCb readcb,
                           boost::asio::io_service *io_service)
    : pollreadcb_(readcb), io_service_(io_service),
      channel_(new IFMapChannel(this, url, user, passwd, certstore)),
      state_machine_(new IFMapStateMachine(this)), ifmap_server_(ifmap_server) {

}

IFMapManager::~IFMapManager() {
}

void IFMapManager::Start() {
    channel_->set_sm(state_machine_.get());

    state_machine_->set_channel(channel_.get());
    state_machine_->Start();
}

void IFMapManager::SetChannel(IFMapChannel *channel) {
    channel_.reset(channel);
}

uint64_t IFMapManager::GetChannelSequenceNumber() {
    return channel_->get_sequence_number();
}

void IFMapManager::GetPeerServerInfo(IFMapPeerServerInfo &server_info) {
    server_info.set_url(channel()->get_url());
    server_info.set_connection_status(channel()->get_connection_status());
}
