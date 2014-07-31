/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __SYSLOG_COLLECTOR_H__
#define __SYSLOG_COLLECTOR_H__

#include "io/tcp_server.h"
#include "io/tcp_session.h"
#include "io/udp_server.h"
#include "io/io_log.h"
#include "viz_message.h"

class DbHandler;

typedef boost::function<bool(const VizMsg*, bool,
    DbHandler *)> VizCallback;

class SyslogQueueEntry
{
  public:
    size_t                     length;
    boost::asio::const_buffer  data;
    std::string                ip;
    int                        port;
    virtual void free ();
    SyslogQueueEntry (boost::asio::const_buffer d, size_t l,
        std::string ip_, int port_):
        length(l), data(d), ip (ip_), port (port_)
    {
    }
    virtual ~SyslogQueueEntry() {}
};

typedef boost::function<void(SyslogQueueEntry *)> SyslogMsgReadFn;

class SyslogTcpSession;

class SyslogTcpListener : public TcpServer
{
    public:
      SyslogTcpListener (EventManager *evm, SyslogMsgReadFn read_cb);
      virtual TcpSession *AllocSession(Socket *socket);
      virtual void Start (std::string ipaddress, int port);
      virtual void Shutdown ();
      virtual void ReadMsg(SyslogQueueEntry *sqe);

    private:
      SyslogTcpSession *session_;
      SyslogMsgReadFn read_cb_;
};

class SyslogUDPListener: public UdpServer
{
    public:
      SyslogUDPListener (EventManager *evm, SyslogMsgReadFn read_cb);
      virtual void Start (std::string ipaddress, int port);
      virtual void Shutdown ();

    private:
      void HandleReceive (boost::asio::const_buffer &recv_buffer,
            boost::asio::ip::udp::endpoint remote_endpoint,
            std::size_t bytes_transferred,
            const boost::system::error_code& error);
      SyslogMsgReadFn read_cb_;
};

class SyslogParser;

class SyslogListeners
{
    public:
      static const int kDefaultSyslogPort = 514;
      SyslogListeners (EventManager *evm, VizCallback cb,
        DbHandler *db_handler, std::string ipaddress,
        int port=kDefaultSyslogPort);
      SyslogListeners (EventManager *evm, VizCallback cb,
        DbHandler *db_handler, int port=kDefaultSyslogPort);
      virtual void Start ();
      virtual void Shutdown ();
      bool IsRunning ();
      VizCallback ProcessSandeshMsgCb() const { return cb_; }
      DbHandler *GetDbHandler () { return db_handler_; }
      SandeshMessageBuilder *GetBuilder () const { return builder_; }
      int GetTcpPort();
      int GetUdpPort();
    private:
      boost::scoped_ptr<SyslogParser> parser_;
      SyslogUDPListener *udp_listener_;
      SyslogTcpListener *tcp_listener_;
      int           port_;
      std::string   ipaddress_;
      bool          inited_;
      VizCallback   cb_;
      DbHandler    *db_handler_;
      SandeshMessageBuilder *builder_;
};

#endif // __SYSLOG_COLLECTOR_H__
