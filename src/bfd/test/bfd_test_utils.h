/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */
#ifndef BFD_TEST_UTILS_H_
#define BFD_TEST_UTILS_H_

#include "bfd/bfd_control_packet.h"
#include "bfd/bfd_connection.h"
#include "bfd/bfd_server.h"

#include <stdint.h>
#include <map>
#include <iomanip>
#include <boost/asio.hpp>
#include <boost/random.hpp>
#include <boost/function.hpp>
#include <boost/thread/thread.hpp>
#include <base/proto.h>
#include <testing/gunit.h>
#include <base/timer.h>
#include <io/event_manager.h>
#include <base/test/task_test_util.h>

namespace BFD {
class EventManagerThread {
    EventManager *evm;
    boost::thread thread;

 public:
    explicit EventManagerThread(EventManager *evm) : evm(evm), thread(boost::bind(&EventManager::Run, evm)) {
    }

    void Stop() {
        evm->Shutdown();
        thread.join();
    }

    ~EventManagerThread() {
        Stop();
    }
};

class TestCommunicatorManager {
 public:
    typedef boost::function<void(const ControlPacket *)> callback;
    typedef std::map<boost::asio::ip::address, callback> Servers;
    Servers servers;
    boost::asio::io_service *io_service;

    explicit TestCommunicatorManager(boost::asio::io_service *io_service)
            : io_service(io_service) {}


    static void processPacketAndFreeActual(const callback &cb,
                                     const ControlPacket *controlPacket) {
        cb(controlPacket);
        delete controlPacket;
    }
    static void processPacketAndFree(const callback &cb,
                                     const ControlPacket *controlPacket) {
        task_util::TaskFire(boost::bind(
                    &TestCommunicatorManager::processPacketAndFreeActual,
                    cb, controlPacket), "BFD");
    }

    void sendPacket(const boost::asio::ip::address &srcAddr,
                    const boost::asio::ip::udp::endpoint &remote_endpoint,
                    const boost::asio::mutable_buffer &packet, int pktSize) {
        Servers::const_iterator it = servers.find(remote_endpoint.address());
        if (it == servers.end())
            return;

        ControlPacket *recvPacket = ParseControlPacket(
            boost::asio::buffer_cast<const uint8_t *>(packet), pktSize);
        recvPacket->remote_endpoint.address(srcAddr);
        recvPacket->local_endpoint.port(remote_endpoint.port());
        recvPacket->length = kMinimalPacketLength;
        io_service->post(boost::bind(&processPacketAndFree, it->second,
                                     recvPacket));
    }

    void registerServer(const boost::asio::ip::address &addr, callback cb) {
        servers[addr] = cb;
    }

    void unregisterServer(const boost::asio::ip::address &addr) {
        servers.erase(addr);
    }
};

class TestCommunicator : public Connection {
    TestCommunicatorManager *manager_;
    const boost::asio::ip::address hostAddr_;
    Server *server_;

 public:
    TestCommunicator(TestCommunicatorManager *manager,
                     const boost::asio::ip::address &hostAddr)
      : manager_(manager), hostAddr_(hostAddr) {}

    virtual void SendPacket(
        const boost::asio::ip::udp::endpoint &local_endpoint,
        const boost::asio::ip::udp::endpoint &remote_endpoint,
        const SessionIndex &session_index,
        const boost::asio::mutable_buffer &packet, int pktSize) {
        manager_->sendPacket(hostAddr_, remote_endpoint, packet, pktSize);
    }

    virtual ~TestCommunicator() { }
    virtual void NotifyStateChange(const SessionKey &key, const bool &up) {
    }
    virtual Server *GetServer() const { return server_; }
    virtual void SetServer(Server *server) { server_ = server; }
};

}  // namespace BFD

#endif  // SRC_BFD_TEST_UTILS_H_
