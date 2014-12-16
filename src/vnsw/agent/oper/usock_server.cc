#include "usock_server.h"
#include "nexthop_server.h"

UnixDomainSocketSession::~UnixDomainSocketSession() {
  if (observer_) {
    observer_(this, CLOSE);
  }
}

void
UnixDomainSocketSession::Start ()
{
  if (observer_) {
    observer_(this, WRITE_READY);
  }

  socket_.async_read_some(boost::asio::buffer(data_),
			  boost::bind(&UnixDomainSocketSession::HandleRead,
				      shared_from_this(),
				      boost::asio::placeholders::error,
			  boost::asio::placeholders::bytes_transferred));
}

void
UnixDomainSocketSession::WriteData (char *cp)
{
  if (write_in_progress_) {
    return;
  }

  if (!cp) {
    return;
  }

  int data_len = strlen(cp);
  int pkt_len = 0;

  data_[pkt_len++] = (unsigned char)(data_len >> 24);
  data_[pkt_len++] = (unsigned char)(data_len >> 16);
  data_[pkt_len++] = (unsigned char)(data_len >>  8);
  data_[pkt_len++] = (unsigned char)data_len;

  std::memcpy(&data_[pkt_len], cp, data_len);
  pkt_len += data_len;

  write_in_progress_ = true;
  boost::asio::async_write(socket_,
			   boost::asio::buffer(data_, pkt_len),
			   boost::bind(&UnixDomainSocketSession::HandleWrite,
				       shared_from_this(),
				       boost::asio::placeholders::error));
}

void
UnixDomainSocketSession::HandleRead (const boost::system::error_code& error,
				     size_t bytes_transferred)
{
  if (error) {
    return;
  }
  if (observer_) {
    observer_(this, WRITE_READY);
  }
}

void
UnixDomainSocketSession::HandleWrite (const boost::system::error_code& error)
{
  write_in_progress_ = false;
  if (error) {
    return;
  }
  if (observer_) {
    observer_(this, WRITE_READY);
  }

  socket_.async_read_some(boost::asio::buffer(data_),
			  boost::bind(&UnixDomainSocketSession::HandleRead,
				      shared_from_this(),
				      boost::asio::placeholders::error,
			   boost::asio::placeholders::bytes_transferred));
}

UnixDomainSocketServer::UnixDomainSocketServer (boost::asio::io_service& io,
						const std::string& file)
  : io_service_(io), acceptor_(io, stream_protocol::endpoint(file)),
    session_idspace_(0)
{
  SessionPtr new_session(new UnixDomainSocketSession(io_service_));
  acceptor_.async_accept(new_session->socket(),
	boost::bind(&UnixDomainSocketServer::HandleAccept, this, new_session,
		    boost::asio::placeholders::error));
}

void
UnixDomainSocketServer::HandleAccept (SessionPtr session,
				      const boost::system::error_code& error)
{
  UnixDomainSocketSession *socket_session = session.get();

  if (error) {
    if (observer_) {
      observer_(this, socket_session, DELETE_SESSION);
    }
    return;
  }

  socket_session->set_session_id(++session_idspace_);
  if (observer_) {
    observer_(this, socket_session, NEW_SESSION);
    session->Start();
  }

  SessionPtr new_session(new UnixDomainSocketSession(io_service_));
  acceptor_.async_accept(new_session->socket(),
	       boost::bind(&UnixDomainSocketServer::HandleAccept, this,
			   new_session, boost::asio::placeholders::error));
}
