/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_ifmap_manager_h
#define ctrlplane_ifmap_manager_h

#include <boost/function.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/scoped_ptr.hpp>

class IFMapChannel;
class IFMapStateMachine;
class IFMapServer;
class IFMapPeerServerInfo;

// This class is the window to the ifmap server for the rest of the system
class IFMapManager {
public:
    typedef boost::function<void(const char *data, size_t length,
                                 uint64_t sequence_number)> PollReadCb;

    IFMapManager(IFMapServer *ifmap_server, const std::string& url,
                 const std::string& user, const std::string& passwd,
                 const std::string& certstore, PollReadCb readcb,
                 boost::asio::io_service *io_service);

    ~IFMapManager();

    void Start();

    boost::asio::io_service *io_service() { return io_service_; }
    IFMapChannel *channel() { return channel_.get(); }
    IFMapStateMachine *state_machine() { return state_machine_.get(); }
    PollReadCb pollreadcb() { return pollreadcb_; }
    void SetChannel(IFMapChannel *channel);
    IFMapServer *ifmap_server() { return ifmap_server_; }
    uint64_t GetChannelSequenceNumber();
    void GetPeerServerInfo(IFMapPeerServerInfo &server_info);

private:

    PollReadCb pollreadcb_;
    boost::asio::io_service *io_service_;
    boost::scoped_ptr<IFMapChannel> channel_;
    boost::scoped_ptr<IFMapStateMachine> state_machine_;
    IFMapServer *ifmap_server_;
};

#endif // ctrlplane_ifmap_manager_h
