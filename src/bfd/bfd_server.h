/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */
#ifndef BFD_SERVER_H_
#define BFD_SERVER_H_

#include "bfd/bfd_common.h"

#include <tbb/mutex.h>
#include <map>
#include <boost/asio/ip/address.hpp>

class EventManager;

namespace BFD {

class Connection;
class BFDSession;
class ControlPacket;
class BFDSessionConfig;

class BFDServer {
    tbb::mutex mutex_;

    EventManager *evm_;
    Connection *communicator_;
    BFDSession *GetSession(const ControlPacket *packet);

 public:
    BFDServer(EventManager *_evm, Connection *communicator) :
        evm_(_evm), communicator_(communicator), sessionManager_(_evm) {}

    ResultCode processControlPacket(const ControlPacket *packet);
    void createSession(const boost::asio::ip::address &remoteHost,
            const BFDSessionConfig *config, Discriminator *assignedDiscriminator);
    BFDSession *sessionByAddress(const boost::asio::ip::address &address);

 private:
    class SessionManager : boost::noncopyable {
        EventManager *evm_;
        typedef std::map<Discriminator, BFDSession*> DiscriminatorSessionMap;
        typedef std::map<boost::asio::ip::address, BFDSession*> AddressSessionMap;

        DiscriminatorSessionMap by_discriminator_;
        AddressSessionMap by_address_;
     public:
        explicit SessionManager(EventManager *evm) : evm_(evm) {}

        BFDSession *SessionByDiscriminator(Discriminator discriminator);
        BFDSession *SessionByAddress(const boost::asio::ip::address &address);
        ResultCode RemoveSession(Discriminator discriminator);
        ResultCode CreateOrUpdateSession(const boost::asio::ip::address &remoteHost,
                const BFDSessionConfig *config,
                Connection *communicator,
                Discriminator *assignedDiscriminator);
        Discriminator GenerateUniqDiscriminator();

        ~SessionManager();
    };
    SessionManager sessionManager_;
};

}  // namespace BFD

#endif /* BFD_SERVER_H_ */
