/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __IO_NETLINK_H__
#define __IO_NETLINK_H__
#include <sys/socket.h>
#include <net/if.h>
#include <net/route.h>
#include <netinet/if_ether.h>
#include <iostream>

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/asio/netlink_protocol.hpp>

using namespace boost::asio;

class EventManager;

struct	RtMsg{
    struct	rt_msghdr m_rtm;
	char	m_space[512];
};

class netlink_sock {
public:
    netlink_sock(boost::asio::io_service &ios) : socket_(ios) {
        socket_.open();
        RtMsgTest();
    }

    netlink_sock(boost::asio::io_service &ios, int proto) : 
        socket_(ios, proto) {
        RtMsgTest();
    }

    int RtMsgEncode() {
        struct rt_msghdr        *rtm = &msg_.m_rtm;
        struct sockaddr_inarp   dst;

        bzero(&msg_, sizeof(msg_));
        bzero(&dst, sizeof(dst));
        dst.sin_len = sizeof(dst);
        dst.sin_family = AF_INET;
        dst.sin_addr.s_addr = inet_addr("192.168.0.5");

        rtm->rtm_flags = 0;
        rtm->rtm_version = RTM_VERSION;
        rtm->rtm_addrs |= RTA_DST;
        rtm->rtm_seq = ++seqno_;
        rtm->rtm_type = RTM_GET;
        bcopy(&dst, msg_.m_space, sizeof(dst));
        rtm->rtm_msglen = sizeof(struct rt_msghdr) + sizeof(dst);
        return rtm->rtm_msglen;
    }

    void RtMsgTest() {
        int     len;
        len = RtMsgEncode();

        socket_.async_send(boost::asio::buffer(&msg_, len),
                   boost::bind(&netlink_sock::write_handler, this,
                               placeholders::error,
                               placeholders::bytes_transferred)
                );
    }


    void read_handler(const boost::system::error_code& error,
                      size_t bytes_transferred) {
        std::cout << "Came in READ_HANDLER. Bytes read " << bytes_transferred << std::endl;
        if (error) {
            std::cerr << "read error: " <<
                boost::system::system_error(error).what() << std::endl;
            return;
        }
    }

    void write_handler(const boost::system::error_code& error,
                       size_t bytes_transferred) {
        if (error) {
            std::cerr << "Write error: " <<
                boost::system::system_error(error).what() << std::endl;
            return;
        }
        socket_.async_receive(boost::asio::buffer(&msg_, 200),
                   boost::bind(&netlink_sock::read_handler, this,
                               placeholders::error,
                               placeholders::bytes_transferred)
                );
    }

    boost::asio::netlink::raw::socket socket_;
    boost::array<char, 200> buff_;
    RtMsg msg_;
    int seqno_;
    
};

#endif
