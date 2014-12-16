#ifndef _AGENT_OPER_USOCK_SERVER_H_
#define _AGENT_OPER_USOCK_SERVER_H_

#include <cstdio>
#include <iostream>
#include <boost/array.hpp>
#include <boost/bind.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/asio.hpp>
#include <boost/function.hpp>

const int kPDULen = 4096;
const int kPDUHeaderLen = 4;
const int kPDUDataLen = 4092;

#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)

using boost::asio::local::stream_protocol;

typedef unsigned char DataBuffer[kPDULen];

class UnixDomainSocketSession
  : public boost::enable_shared_from_this<UnixDomainSocketSession>
{
public:

  enum Event {
    EVENT_NONE,
    WRITE_READY,
    CLOSE
  };
  typedef boost::function<void(UnixDomainSocketSession *, Event)> EventObserver;

  UnixDomainSocketSession(boost::asio::io_service& io_service)
    : socket_(io_service), write_in_progress_(false), session_id_(0)
  {
  }

  ~UnixDomainSocketSession();

  stream_protocol::socket& socket()
  {
    return socket_;
  }

  bool write_ready() {
    return !write_in_progress_;
  }

  void set_observer(EventObserver observer)
  {
    observer_ = observer;
  }

  uint64_t session_id()
  {
    return session_id_;
  }

  void set_session_id(uint64_t id)
  {
    session_id_ = id;
  }

  void Start();
  void WriteData(char *cp);

private:
  stream_protocol::socket socket_;
  DataBuffer data_;
  EventObserver observer_;
  bool write_in_progress_;
  uint64_t session_id_;

  void HandleRead(const boost::system::error_code& error, size_t bytes);
  void HandleWrite(const boost::system::error_code& error);
};

typedef boost::shared_ptr<UnixDomainSocketSession> SessionPtr;

class UnixDomainSocketServer
{
public:

  enum Event {
    EVENT_NONE,
    NEW_SESSION,
    DELETE_SESSION
  };
  typedef boost::function<void(UnixDomainSocketServer *,
			       UnixDomainSocketSession *, Event)> EventObserver;

  UnixDomainSocketServer(boost::asio::io_service& io_service,
			 const std::string& file);

  void HandleAccept(SessionPtr new_session,
		    const boost::system::error_code& error);

  void set_observer(EventObserver observer)
  {
    observer_ = observer;
  }

private:
  boost::asio::io_service& io_service_;
  EventObserver observer_;
  stream_protocol::acceptor acceptor_;
  uint64_t session_idspace_;
};

#else // defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
# error Local sockets not available on this platform.
#endif // defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)

#endif
