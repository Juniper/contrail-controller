//
// Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
//

#include <cerrno>
#include <cstring>

#include <base/logging.h>

#include <zookeeper/zookeeper_client.h>
#include <zookeeper/zookeeper_client_impl.h>
#include <zookeeper/zookeeper_interface.h>

#define ZOO_LOG(_Level, _Msg)                                             \
    do {                                                                  \
        if (LoggingDisabled()) break;                                     \
        log4cplus::Logger logger = log4cplus::Logger::getRoot();          \
        LOG4CPLUS_##_Level(logger, __func__ << ":" << __FILE__ << ":" <<  \
            __LINE__ << ": " << _Msg);                                    \
    } while (false)

#define ZOO_LOG_ERR(_Msg)                                                 \
    do {                                                                  \
        LOG(ERROR, __func__ << ":" << __FILE__ << ":" << __LINE__ << ": " \
            << _Msg);                                                     \
    } while (false)

namespace zookeeper {
namespace interface {

class ZookeeperCBindings : public ZookeeperInterface {
 public:
    ZookeeperCBindings() {
    }
    virtual ~ZookeeperCBindings() {
    }
    virtual void ZooSetDebugLevel(ZooLogLevel logLevel) {
        zoo_set_debug_level(logLevel);
    }
    virtual zhandle_t* ZookeeperInit(const char *host, watcher_fn fn,
        int recv_timeout, const clientid_t *clientid, void *context,
        int flags) {
        return zookeeper_init(host, fn, recv_timeout, clientid, context,
            flags);
    }
    virtual int ZookeeperClose(zhandle_t *zh) {
        return zookeeper_close(zh);
    }
    virtual int ZooState(zhandle_t *zh) {
        return zoo_state(zh);
    }
    virtual int ZooCreate(zhandle_t *zh, const char *path, const char *value,
        int valuelen, const struct ACL_vector *acl, int flags,
        char *path_buffer, int path_buffer_len) {
        return zoo_create(zh, path, value, valuelen, acl, flags, path_buffer,
            path_buffer_len);
    }
    virtual int ZooDelete(zhandle_t *zh, const char *path, int version) {
        return zoo_delete(zh, path, version);
    }
    virtual int ZooGet(zhandle_t *zh, const char *path, int watch,
        char *buffer, int* buffer_len, struct Stat *stat) {
        return zoo_get(zh, path, watch, buffer, buffer_len, stat);
    }
};

} // namespace interface

namespace client {
namespace impl {

// ZookeeperClientImpl
ZookeeperClientImpl::ZookeeperClientImpl(const char *hostname,
    const char *servers, zookeeper::interface::ZookeeperInterface *zki) :
    hostname_(hostname),
    servers_(servers),
    zk_handle_(NULL),
    connected_(false),
    zki_(zki) {
    // Set loglevel
    zki_->ZooSetDebugLevel(ZOO_LOG_LEVEL_DEBUG);
}

ZookeeperClientImpl::~ZookeeperClientImpl() {
}

bool ZookeeperClientImpl::Connect(bool blocking) {
    while (true) {
        zk_handle_ = zki_->ZookeeperInit(servers_.c_str(),
                                         NULL,
                                         kSessionTimeoutMSec_,
                                         NULL,
                                         NULL,
                                         0);
        if (zk_handle_ == NULL) {
            int zerrno(errno);
            if (!blocking) {
                return false;
            }
            ZOO_LOG_ERR("zookeeper_init FAILED: (" << zerrno <<
                "): servers: " << servers_ << " retrying in 1 second");
            sleep(1);
            continue;
        }
        // Block till session is connected
        while (!connected_) {
            int zstate(zki_->ZooState(zk_handle_));
            if (zstate == ZOO_CONNECTED_STATE) {
                connected_ = true;
                ZOO_LOG(DEBUG, "Session CONNECTED");
                break;
            } else {
                if (!blocking) {
                    return false;
                }
                ZOO_LOG(DEBUG, "Session NOT CONNECTED: retrying in 1 second");
                sleep(1);
                continue;
            }
        }
        break;
    }
    assert(connected_);
    return true;
}

void ZookeeperClientImpl::Shutdown() {
    if (zk_handle_) {
        int rc(zki_->ZookeeperClose(zk_handle_));
        if (rc != ZOK) {
            int zerrno(errno);
            ZOO_LOG(WARN, "zookeeper_close FAILED (" << rc <<
                "): errno: " << zerrno);
        }
        zk_handle_ = NULL;
    }
    connected_ = false;
}

bool ZookeeperClientImpl::Reconnect() {
    Shutdown();
    return Connect();
}

bool ZookeeperClientImpl::IsConnected() const {
    return connected_;
}

static inline bool IsZooErrorRecoverable(int zerror) {
    return zerror == ZCONNECTIONLOSS ||
        zerror == ZOPERATIONTIMEOUT;
}

static inline bool IsZooErrorUnrecoverable(int zerror) {
    return zerror == ZINVALIDSTATE;
}

int ZookeeperClientImpl::CreateNodeSync(const char *path, const char *value,
    int *err, int flag = 0) {
    int rc;
 retry:
    do {
        rc = zki_->ZooCreate(zk_handle_, path, value, strlen(value),
            &ZOO_OPEN_ACL_UNSAFE, flag, NULL, -1);
    } while (IsZooErrorRecoverable(rc));
    if (IsZooErrorUnrecoverable(rc)) {
        // Reconnect
        Reconnect();
        goto retry;
    }
    if (rc != ZOK) {
        *err = errno;
    }
    return rc;
}

int ZookeeperClientImpl::GetNodeDataSync(const char *path, char *buf,
    int *buf_len, int *err) {
    int rc;
 retry:
    do {
        rc = zki_->ZooGet(zk_handle_, path, 0, buf, buf_len, NULL);
    } while (IsZooErrorRecoverable(rc));
    if (IsZooErrorUnrecoverable(rc)) {
        // Reconnect
        Reconnect();
        goto retry;
    }
    if (rc != ZOK) {
        *err = errno;
    }
    return rc;
}

bool ZookeeperClientImpl::CreateNode(const char * path,
                                     int flag,
                                     bool blocking) {
    int err = 0;
    int rc;
    if (!IsConnected()) {
        bool success(Connect(blocking));
        if (!success) {
            ZOO_LOG_ERR("Zookeeper Client Connect FAILED");
            return false;
        }
    }
    if (blocking) {
        rc = CreateNodeSync(path, hostname_.c_str(), &err, flag);
    } else {
       rc = zki_->ZooCreate(zk_handle_, path, hostname_.c_str(),
                            strlen(hostname_.c_str()), &ZOO_OPEN_ACL_UNSAFE,
                            flag, NULL, -1);
    }
    switch (rc) {
        case ZOK: {
            std::string Path(path);
            ZOO_LOG(DEBUG, "CREATE ZNODE:"<< Path << "/" << hostname_);
            break;
        }
        case ZNODEEXISTS: {
            std::string Path(path);
            ZOO_LOG(DEBUG, "CREATE ZNODE:"<< Path << "/" << hostname_ << " exist");
            break;
        }
        default: {
            std::string Path(path);
            ZOO_LOG_ERR("CreateNode(" << Path << "): " << hostname_
                    << ": FAILED: (" << rc << ") error: " << err);
            Shutdown();
            return false;
        }
    }
    return true;
}

bool ZookeeperClientImpl::CheckNodeExist(const char *path) {
    int rc;
    char buf[256];
    int buf_len(sizeof(buf));
    rc = zki_->ZooGet(zk_handle_, path, 0, buf, &buf_len, NULL);
    return (rc == ZOK);
}

int ZookeeperClientImpl::DeleteNodeSync(const char *path, int *err) {
    int rc;
 retry:
    do {
        rc = zki_->ZooDelete(zk_handle_, path, -1);
    } while (IsZooErrorRecoverable(rc));
    if (IsZooErrorUnrecoverable(rc)) {
        // Reconnect
        Reconnect();
        goto retry;
    }
    if (rc != ZOK) {
        *err = errno;
    }
    return rc;
}


std::string ZookeeperClientImpl::Name() const {
    return hostname_;
}

} // namespace impl

// ZookeeperClient
ZookeeperClient::ZookeeperClient(const char *hostname, const char *servers) :
    impl_(new impl::ZookeeperClientImpl(hostname, servers,
        new zookeeper::interface::ZookeeperCBindings)) {
}

ZookeeperClient::ZookeeperClient(impl::ZookeeperClientImpl *impl) :
    impl_(impl) {
}

bool ZookeeperClient::CreateNode(const char * path, int type, bool blocking) {
    int flag = 0;
    if (type == Z_NODE_TYPE_EPHEMERAL) {
        flag |= ZOO_EPHEMERAL;
    }
    if (type == Z_NODE_TYPE_SEQUENCE) {
        flag |= ZOO_SEQUENCE;
    }
    return impl_->CreateNode(path, flag, blocking);
}

bool ZookeeperClient::CheckNodeExist(const char *path) {
    return impl_->CheckNodeExist(path);
}

void ZookeeperClient::Shutdown() {
    return impl_->Shutdown();
}

ZookeeperClient::~ZookeeperClient() {
}

// ZookeeperLockImpl
class ZookeeperLock::ZookeeperLockImpl {
 public:
    ZookeeperLockImpl(impl::ZookeeperClientImpl *clientImpl,
        const char *path) :
        clientImpl_(clientImpl),
        path_(path),
        is_acquired_(false) {
        id_ = clientImpl_->Name();
    }

    std::string Id() const {
        return id_;
    }

    bool Lock() {
        ZOO_LOG(INFO, "Trying (" << path_ << "): " << id_);
        while (true) {
            // Connect if not already done
            if (!clientImpl_->IsConnected()) {
                bool success(clientImpl_->Connect());
                if (!success) {
                    ZOO_LOG_ERR("Zookeeper Client Connect FAILED");
                    return success;
                }
            }
            // Try creating the znode
            int err;
            int rc(clientImpl_->CreateNodeSync(path_.c_str(), id_.c_str(),
                &err));
            switch (rc) {
              case ZOK: {
                // We acquired the lock
                ZOO_LOG(INFO, "ACQUIRED (" << path_ << "): " << id_);
                is_acquired_ = true;
                return true;
              }
              case ZNODEEXISTS: {
                // Node exists, get node data and check
                char buf[256];
                int buf_len(sizeof(buf));
                int zerr;
                int zrc(clientImpl_->GetNodeDataSync(path_.c_str(), buf,
                    &buf_len, &zerr));
                if (zrc == ZOK) {
                    // Does it match our ID?
                    std::string mid(buf, buf_len);
                    if (id_ == mid) {
                        // We acquired the lock
                        ZOO_LOG(INFO, "ACQUIRED EEXIST (" << path_ << "): "
                            << id_);
                        is_acquired_ = true;
                        return true;
                    }
                    ZOO_LOG(INFO, "EEXIST (" << path_ << "): " << mid <<
                        " , ours: " << id_);
                    sleep(1);
                    continue;
                } else if (zrc == ZNONODE) {
                    ZOO_LOG(WARN, "GetNodeDataSync(" << path_ <<
                        "): Data: " << id_ <<
                        ": No Node EXISTS: retrying in 1 second");
                    sleep(1);
                    continue;
                } else {
                    ZOO_LOG_ERR("GetNodeDataSync(" << path_ << "): " <<
                        id_ << ": FAILED: (" << zrc << ") error: " << zerr);
                    clientImpl_->Shutdown();
                    return false;
                }
                break;
              }
              default: {
                ZOO_LOG_ERR("CreateNodeSync(" << path_ << "): " << id_
                    << ": FAILED: (" << rc << ") error: " << err);
                clientImpl_->Shutdown();
                return false;
              }
            }
        }
    }

    bool Release() {
        bool success;
        int err, rc;
        if (!is_acquired_) {
            ZOO_LOG_ERR("(" << path_ << "): " << id_ <<
                ": Release WITHOUT Lock");
            success = false;
            goto cleanup;
        }
        is_acquired_ = false;
        rc = clientImpl_->DeleteNodeSync(path_.c_str(), &err);
        if (rc == ZOK) {
            ZOO_LOG(INFO, "RELEASED (" << path_ << "): " << id_);
            success = true;
            goto cleanup;
        } else if (rc == ZNONODE) {
            ZOO_LOG_ERR("DeleteNodeSync(" << path_ << "): " << id_ <<
                ": No Node EXISTS(" << err <<
                "): Possible concurrent execution");
            success = false;
            goto cleanup;
        } else {
            ZOO_LOG_ERR("DeleteNodeSync(" << path_ << "): " << id_ <<
                ": FAILED (" << rc << "): error " << err);
            success = false;
            goto cleanup;
        }
     cleanup:
        clientImpl_->Shutdown();
        return success;
    }

 private:
    impl::ZookeeperClientImpl *clientImpl_;
    std::string path_;
    bool is_acquired_;
    std::string id_;
};

// ZookeeperLock
ZookeeperLock::ZookeeperLock(ZookeeperClient *client, const char *path) :
    impl_(new ZookeeperLockImpl(client->impl_.get(), path)) {
}

ZookeeperLock::~ZookeeperLock() {
}

std::string ZookeeperLock::Id() const {
    return impl_->Id();
}

bool ZookeeperLock::Lock() {
    return impl_->Lock();
}

bool ZookeeperLock::Release() {
    return impl_->Release();
}

} // namespace client
} // namespace zookeeper
