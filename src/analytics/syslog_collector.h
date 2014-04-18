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

class SyslogTcpSession;

class SyslogTcpListener : public TcpServer
{
    public:
      explicit SyslogTcpListener (EventManager *evm);
      virtual TcpSession *AllocSession(Socket *socket);
      virtual void Start (std::string ipaddress, int port);
      virtual void Shutdown ();

      virtual void Parse (SyslogQueueEntry *sqe) = 0;
    private:
      SyslogTcpSession *session_;
};

class SyslogUDPListener: public UDPServer
{
    public:
      SyslogUDPListener (EventManager *evm);
      virtual void Start (std::string ipaddress, int port);
      virtual void Shutdown ();
    protected:
      virtual void Parse (SyslogQueueEntry *sqe) = 0;
    private:

      void HandleReceive (boost::asio::const_buffer recv_buffer,
            udp::endpoint remote_endpoint, std::size_t bytes_transferred,
            const boost::system::error_code& error);
};

class SyslogParser;

class SyslogListeners : public SyslogUDPListener,
    public SyslogTcpListener
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
    protected:
      virtual void Parse (SyslogQueueEntry *sqe);
    private:
      boost::scoped_ptr<SyslogParser> parser_;
      int           port_;
      std::string   ipaddress_;
      bool          inited_;
      VizCallback   cb_;
      DbHandler    *db_handler_;
      SandeshMessageBuilder *builder_;
};


#endif // __SYSLOG_COLLECTOR_H__
