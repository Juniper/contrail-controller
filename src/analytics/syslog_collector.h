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
#include "db_handler.h"
#include "config_client_collector.h"


typedef boost::function<bool(const VizMsg*, bool,
    DbHandler *, GenDb::GenDbIf::DbAddColumnCb)> VizCallback;

class SyslogParser;
class SyslogGenerator;
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
      void HandleReceive(const boost::asio::const_buffer &recv_buffer,
            boost::asio::ip::udp::endpoint remote_endpoint,
            std::size_t bytes_transferred,
            const boost::system::error_code& error);
      SyslogMsgReadFn read_cb_;
};

class SyslogListeners
{
    public:
      static const int kDefaultSyslogPort = 514;
      SyslogListeners (EventManager *evm, VizCallback cb,
        DbHandlerPtr db_handler, std::string ipaddress,
        int port=kDefaultSyslogPort);
      SyslogListeners (EventManager *evm, VizCallback cb,
        DbHandlerPtr db_handler, int port=kDefaultSyslogPort);
      virtual void Start ();
      virtual void Shutdown ();
      bool IsRunning ();
      VizCallback ProcessSandeshMsgCb() const { return cb_; }
      DbHandlerPtr GetDbHandler () const { return db_handler_; }
      SandeshMessageBuilder *GetBuilder () const { return builder_; }
      //StructuredSyslogConfig *GetStructuredSyslogConfig() {
      //  return structured_syslog_config_;
   // }
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
      DbHandlerPtr    db_handler_;
      SandeshMessageBuilder *builder_;
      
};

class SyslogParser
{

    public:
        SyslogParser (SyslogListeners *syslog);
        virtual ~SyslogParser ();
        void Parse (SyslogQueueEntry *sqe);

        void Shutdown ();

        SyslogParser ();

        void Init();
        void WaitForIdle (int max_wait);

        enum dtype {
            int_type = 42,
            str_type
        };
        struct Holder {
            std::string       key;
            dtype             type;
            int64_t           i_val;
            std::string       s_val;

            Holder (std::string k, std::string v):
                key(k), type(str_type), s_val(v)
            { }
            Holder (std::string k, int64_t v):
                key(k), type(int_type), i_val(v)
            { }

            std::string repr()
            {
                std::ostringstream s;
                s << "{ \"" << key << "\": ";
                if (type == int_type)
                    s << i_val << "}";
                else if (type == str_type)
                    s << "\"" << s_val << "\"}";
                else
                    s << "**bad type**}";
                return s.str();
            }

            void print ()
            {
                LOG(DEBUG, "{ \"" << key << "\": ");
                if (type == int_type)
                    LOG(DEBUG, i_val << "}");
                else if (type == str_type)
                    LOG(DEBUG, "\"" << s_val << "\"}");
                else
                    LOG(DEBUG, "**bad type**}");
            }
        };

        typedef std::map<std::string, Holder>  syslog_m_t;

        template <typename Iterator>
        static bool parse_syslog (Iterator start, Iterator end, syslog_m_t &v);

        static std::string GetMapVals (syslog_m_t v, std::string key, std::string def);

        static int64_t GetMapVal (syslog_m_t v, std::string key, int def);

        static void GetFacilitySeverity (syslog_m_t v, int& facility, int& severity);

        static void GetTimestamp (syslog_m_t v, time_t& timestamp);

        static void PostParsing (syslog_m_t &v);

        SyslogGenerator *GetGenerator (std::string ip);

        std::string GetSyslogFacilityName (uint64_t f);

        std::string EscapeXmlTags (std::string text);

        std::string GetMsgBody (syslog_m_t v);

        std::string GetModule(syslog_m_t v);

        std::string GetFacility(syslog_m_t v);

        int GetPID(syslog_m_t v);

    protected:
        virtual void MakeSandesh (syslog_m_t v);

        bool ClientParse (SyslogQueueEntry *sqe);
    private:
        WorkQueue<SyslogQueueEntry*>                 work_queue_;
        boost::uuids::random_generator               umn_gen_;
        boost::ptr_map<std::string, SyslogGenerator> genarators_;
        SyslogListeners                             *syslog_;
        std::vector<std::string>                     facilitynames_;
};



#endif // __SYSLOG_COLLECTOR_H__
