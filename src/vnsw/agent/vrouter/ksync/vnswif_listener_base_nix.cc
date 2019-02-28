#include "vrouter/ksync/vnswif_listener_base_nix.h"

VnswInterfaceListenerBaseNix::VnswInterfaceListenerBaseNix(Agent *agent) : VnswInterfaceListenerBase(agent),
    sock_(*(agent->event_manager())->io_service()), sock_fd_(-1) {
}

void VnswInterfaceListenerBaseNix::Init() {
    VnswInterfaceListenerBase::Init();
    /* Create socket and listen and handle ip address updates */
    if (agent_->test_mode()) {
        return;
    }

    sock_fd_ = CreateSocket();

    /* Assign native socket to boost asio */
    boost::asio::local::datagram_protocol protocol;
    sock_.assign(protocol, sock_fd_);

    SyncCurrentState();

    RegisterAsyncReadHandler();
}

void VnswInterfaceListenerBaseNix::Shutdown() {
    VnswInterfaceListenerBase::Shutdown();
    if (agent_->test_mode()) {
        return;
    }

    boost::system::error_code ec;
    sock_.close(ec);
}

VnswInterfaceListenerBase::~VnswInterfaceListenerBase() {}