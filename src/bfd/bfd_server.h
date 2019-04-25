/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */

#ifndef SRC_BFD_BFD_SERVER_H_
#define SRC_BFD_BFD_SERVER_H_

#include "base/queue_task.h"
#include "bfd/bfd_common.h"

#include <map>
#include <set>
#include <boost/asio.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/scoped_ptr.hpp>

class EventManager;

namespace BFD {
class Connection;
class Session;
struct ControlPacket;
struct SessionConfig;

typedef std::set<SessionKey> Sessions;

// This class manages sessions with other BFD peers.
class Server {
 struct Event;
 public:
    Server(EventManager *evm, Connection *communicator);
    virtual ~Server();
    ResultCode ProcessControlPacketActual(const ControlPacket *packet);
    void ProcessControlPacket(
        const boost::asio::ip::udp::endpoint &local_endpoint,
        const boost::asio::ip::udp::endpoint &remote_endpoint,
        const SessionIndex &session_index,
        const boost::asio::const_buffer &recv_buffer,
        std::size_t bytes_transferred, const boost::system::error_code& error);

    // If a BFD session with specified [remoteHost] already exists, its
    // configuration is updated with [config], otherwise it gets created.
    // ! TODO implement configuration update
    ResultCode ConfigureSession(const SessionKey &key,
                                const SessionConfig &config,
                                Discriminator *assignedDiscriminator);

    // Instances of BFD::Session are removed after last IP address
    // reference is gone.
    ResultCode RemoveSessionReference(const SessionKey &key);
    Session *SessionByKey(const boost::asio::ip::address &address,
                          const SessionIndex &index = SessionIndex());
    Session *SessionByKey(const SessionKey &key);
    Session *SessionByKey(const SessionKey &key) const;
    Connection *communicator() const { return communicator_; }
    void AddSession(const SessionKey &key, const SessionConfig &config,
                       ChangeCb cb);
    void DeleteSession(const SessionKey &key);
    void DeleteClientSessions();
    Sessions *GetSessions() { return &sessions_; }
    WorkQueue<Event *> *event_queue() { return event_queue_.get(); }

 private:
    class SessionManager : boost::noncopyable {
     public:
        explicit SessionManager(EventManager *evm) : evm_(evm) {}
        ~SessionManager();

        ResultCode ConfigureSession(const SessionKey &key,
                                    const SessionConfig &config,
                                    Connection *communicator,
                                    Discriminator *assignedDiscriminator);

        // see: Server:RemoveSessionReference
        ResultCode RemoveSessionReference(const SessionKey &key);

        Session *SessionByDiscriminator(Discriminator discriminator);
        Session *SessionByKey(const SessionKey &key);
        Session *SessionByKey(const SessionKey &key) const;

     private:
        typedef std::map<Discriminator, Session *> DiscriminatorSessionMap;
        typedef std::map<SessionKey, Session *> KeySessionMap;
        typedef std::map<Session *, unsigned int> RefcountMap;

        Discriminator GenerateUniqueDiscriminator();

        EventManager *evm_;
        DiscriminatorSessionMap by_discriminator_;
        KeySessionMap by_key_;
        RefcountMap refcounts_;
    };

    enum EventType {
        BEGIN_EVENT,
        ADD_CONNECTION = BEGIN_EVENT,
        DELETE_CONNECTION,
        DELETE_CLIENT_CONNECTIONS,
        PROCESS_PACKET,
        END_EVENT = PROCESS_PACKET,
    };

    struct Event {
        Event(EventType type, const SessionKey &key,
              const SessionConfig &config, ChangeCb cb) :
                type(type), key(key), config(config), cb(cb) {
        }
        Event(EventType type, const SessionKey &key) :
                type(type), key(key) {
        }
        Event(EventType type, boost::asio::ip::udp::endpoint local_endpoint,
              boost::asio::ip::udp::endpoint remote_endpoint,
              const SessionIndex &session_index,
              const boost::asio::const_buffer &recv_buffer,
              std::size_t bytes_transferred) :
                type(type), local_endpoint(local_endpoint),
                remote_endpoint(remote_endpoint), session_index(session_index),
                recv_buffer(recv_buffer), bytes_transferred(bytes_transferred) {
        }
        Event(EventType type) : type(type) {
        }

        EventType type;
        SessionKey key;
        SessionConfig config;
        ChangeCb cb;
        boost::asio::ip::udp::endpoint local_endpoint;
        boost::asio::ip::udp::endpoint remote_endpoint;
        const SessionIndex session_index;
        const boost::asio::const_buffer recv_buffer;
        std::size_t bytes_transferred;
    };

    void AddSession(Event *event);
    void DeleteSession(Event *event);
    void DeleteClientSessions(Event *event);
    void ProcessControlPacket(Event *event);
    void EnqueueEvent(Event *event);
    bool EventCallback(Event *event);

    Session *GetSession(const ControlPacket *packet);

    EventManager *evm_;
    Connection *communicator_;
    SessionManager session_manager_;
    boost::scoped_ptr<WorkQueue<Event *> > event_queue_;
    Sessions sessions_;
};

}  // namespace BFD

#endif  // SRC_BFD_BFD_SERVER_H_
