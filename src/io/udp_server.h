/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <boost/asio.hpp>
#include <io/event_manager.h>
#include <boost/intrusive_ptr.hpp>
using boost::asio::ip::udp;
using boost::asio::mutable_buffer;


class UDPServer {
public:
    enum ServerState {
        OK = 42,
        Uninitialized,
        SocketOpenFailed,
        SocketBindFailed,
    };
    typedef boost::intrusive_ptr<UDPServer> UDPServerPtr;

    static const int kDefaultBufferSize = 4 * 1024;

    explicit UDPServer (EventManager *evm, int buffer_size=kDefaultBufferSize);
    explicit UDPServer (boost::asio::io_service &io_service,
                            int buffer_size=kDefaultBufferSize);
    virtual ~UDPServer ();

    virtual void Initialize(short port);
    virtual void Initialize(std::string ipaddress, short port);
    virtual void Initialize(udp::endpoint local_endpoint);
    virtual void Reset ();
    virtual void Shutdown ();

    virtual udp::endpoint GetLocalEndPoint ();
    //tx-rx
    void StartSend(udp::endpoint &ep, std::size_t bytes_to_send,
            mutable_buffer buffer);
    virtual void HandleReceive (
            boost::asio::const_buffer recv_buffer, 
            udp::endpoint remote_endpoint,
            std::size_t bytes_transferred,
            const boost::system::error_code& error);
    void StartReceive();
    virtual void HandleSend (boost::asio::const_buffer send_buffer,
            udp::endpoint remote_endpoint, std::size_t bytes_transferred,
            const boost::system::error_code& error);

    // state
    ServerState GetServerState () { return state_; }
    udp::endpoint GetLocalEndpoint(boost::system::error_code &error);
    std::string GetLocalEndpointAddress();
    int GetLocalEndpointPort();


    //buffers
    mutable_buffer AllocateBuffer();
    mutable_buffer AllocateBuffer(std::size_t s);
    void DeallocateBuffer (boost::asio::const_buffer &buffer);

protected:
    EventManager *event_manager() { return evm_; }
    virtual bool DisableSandeshLogMessages() { return false; }
    virtual std::string ToString() { return name_; }
private:
    virtual void SetName (udp::endpoint ep);
    void HandleReceiveInternal (
            boost::asio::const_buffer recv_buffer, 
            std::size_t bytes_transferred,
            const boost::system::error_code& error);
    DISALLOW_COPY_AND_ASSIGN(UDPServer);
    boost::asio::ip::udp::socket socket_;
    int buffer_size_;
    ServerState state_;
    EventManager *evm_;
    std::string name_;
    udp::endpoint remote_endpoint_;
    std::vector<u_int8_t *> pbuf_;
};
