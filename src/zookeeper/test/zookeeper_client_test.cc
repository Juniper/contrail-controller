//
// Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
//

#include <testing/gunit.h>

#include <base/logging.h>
#include <zookeeper/zookeeper_interface.h>
#include <zookeeper/zookeeper_client_impl.h>
#include <zookeeper/zookeeper_client.h>

using ::testing::Return;
using ::testing::WithArgs;
using ::testing::DoAll;
using ::testing::SetArgPointee;
using ::testing::_;
using ::testing::StrEq;

using namespace zookeeper::client;
using namespace zookeeper::interface;

class ZookeeperMockInterface : public ZookeeperInterface {
 public:
    ZookeeperMockInterface() {
    }
    virtual ~ZookeeperMockInterface() {
    }
    MOCK_METHOD1(ZooSetDebugLevel, void(ZooLogLevel logLevel));
    MOCK_METHOD6(ZookeeperInit, zhandle_t*(const char *host, watcher_fn fn,
        int recv_timeout, const clientid_t *clientid, void *context,
        int flags));
    MOCK_METHOD1(ZookeeperClose, int(zhandle_t *zh));
    MOCK_METHOD1(ZooState, int(zhandle_t *zh));
    MOCK_METHOD8(ZooCreate, int(zhandle_t *zh, const char *path,
        const char *value, int valuelen, const struct ACL_vector *acl,
        int flags, char *path_buffer, int path_buffer_len));
    MOCK_METHOD3(ZooDelete, int(zhandle_t *zh, const char *path,
        int version));
    MOCK_METHOD6(ZooGet, int(zhandle_t *zh, const char *path, int watch,
        char *buffer, int* buffer_len, struct Stat *stat));
};

class ZookeeperClientTest : public ::testing::Test {
 protected:
    ZookeeperClientTest() {
    }
    ~ZookeeperClientTest() {
    }
    virtual void SetUp() {
    }
    virtual void TearDown() {
    }
    ZookeeperClient* CreateClient(impl::ZookeeperClientImpl *impl) {
        return new ZookeeperClient(impl);
    }
    std::string GetLockId(const ZookeeperLock &zk_lock) {
        return zk_lock.Id();
    }
};

TEST_F(ZookeeperClientTest, Basic) {
    ZookeeperMockInterface *zmi(new ZookeeperMockInterface);
    EXPECT_CALL(*zmi, ZooSetDebugLevel(_));
    impl::ZookeeperClientImpl *cImpl(
        new impl::ZookeeperClientImpl("Test", "127.0.0.1:2181", zmi));
    std::auto_ptr<ZookeeperClient> client(CreateClient(cImpl));
    std::string zk_lock_name("/test-lock");
    ZookeeperLock zk_lock(client.get(), zk_lock_name.c_str());
    std::string zk_lock_id(GetLockId(zk_lock));
    int zkh(0xdeadbeef);
    zhandle_t *zk_handle = (zhandle_t *)(&zkh);
    EXPECT_CALL(*zmi, ZookeeperInit(StrEq("127.0.0.1:2181"), _, _, _, _, _))
        .WillOnce(Return(zk_handle));
    EXPECT_CALL(*zmi, ZooState(zk_handle))
        .WillOnce(Return(ZOO_CONNECTED_STATE));
    EXPECT_CALL(*zmi, ZooCreate(zk_handle, StrEq(zk_lock_name),
        StrEq(zk_lock_id), zk_lock_id.length(), _, _, _, _))
        .WillOnce(Return(ZOK));
    EXPECT_TRUE(zk_lock.Lock());
    EXPECT_TRUE(cImpl->IsConnected());
    EXPECT_CALL(*zmi, ZooDelete(zk_handle, StrEq(zk_lock_name), _))
        .WillOnce(Return(ZOK));
    EXPECT_CALL(*zmi, ZookeeperClose(zk_handle));
    EXPECT_TRUE(zk_lock.Release());
    EXPECT_FALSE(cImpl->IsConnected());
}

TEST_F(ZookeeperClientTest, ZookeeperInitFail) {
    ZookeeperMockInterface *zmi(new ZookeeperMockInterface);
    EXPECT_CALL(*zmi, ZooSetDebugLevel(_));
    impl::ZookeeperClientImpl *cImpl(
        new impl::ZookeeperClientImpl("Test", "127.0.0.1:2181", zmi));
    std::auto_ptr<ZookeeperClient> client(CreateClient(cImpl));
    std::string zk_lock_name("/test-lock");
    ZookeeperLock zk_lock(client.get(), zk_lock_name.c_str());
    std::string zk_lock_id(GetLockId(zk_lock));
    EXPECT_CALL(*zmi, ZookeeperInit(StrEq("127.0.0.1:2181"), _, _, _, _, _));
    EXPECT_FALSE(zk_lock.Lock());
    EXPECT_FALSE(cImpl->IsConnected());
}

TEST_F(ZookeeperClientTest, ZooStateConnecting2Connect) {
    ZookeeperMockInterface *zmi(new ZookeeperMockInterface);
    EXPECT_CALL(*zmi, ZooSetDebugLevel(_));
    impl::ZookeeperClientImpl *cImpl(
        new impl::ZookeeperClientImpl("Test", "127.0.0.1:2181", zmi));
    std::auto_ptr<ZookeeperClient> client(CreateClient(cImpl));
    std::string zk_lock_name("/test-lock");
    ZookeeperLock zk_lock(client.get(), zk_lock_name.c_str());
    std::string zk_lock_id(GetLockId(zk_lock));
    int zkh(0xdeadbeef);
    zhandle_t *zk_handle = (zhandle_t *)(&zkh);
    EXPECT_CALL(*zmi, ZookeeperInit(StrEq("127.0.0.1:2181"), _, _, _, _, _))
        .WillOnce(Return(zk_handle));
    EXPECT_CALL(*zmi, ZooState(zk_handle))
        .WillOnce(Return(ZOO_CONNECTING_STATE))
        .WillOnce(Return(ZOO_CONNECTED_STATE));
    EXPECT_CALL(*zmi, ZooCreate(zk_handle, StrEq(zk_lock_name),
        StrEq(zk_lock_id), zk_lock_id.length(), _, _, _, _))
        .WillOnce(Return(ZOK));
    EXPECT_TRUE(zk_lock.Lock());
    EXPECT_TRUE(cImpl->IsConnected());
    EXPECT_CALL(*zmi, ZooDelete(zk_handle, StrEq(zk_lock_name), _))
        .WillOnce(Return(ZOK));
    EXPECT_CALL(*zmi, ZookeeperClose(zk_handle));
    EXPECT_TRUE(zk_lock.Release());
    EXPECT_FALSE(cImpl->IsConnected());
}

ACTION_P(StrCpyToArg0, str) {
    std::strcpy(arg0, str);
}

TEST_F(ZookeeperClientTest, ZooCreateNodeExists) {
    ZookeeperMockInterface *zmi(new ZookeeperMockInterface);
    EXPECT_CALL(*zmi, ZooSetDebugLevel(_));
    impl::ZookeeperClientImpl *cImpl(
        new impl::ZookeeperClientImpl("Test", "127.0.0.1:2181", zmi));
    std::auto_ptr<ZookeeperClient> client(CreateClient(cImpl));
    std::string zk_lock_name("/test-lock");
    ZookeeperLock zk_lock(client.get(), zk_lock_name.c_str());
    std::string zk_lock_id(GetLockId(zk_lock));
    int zkh(0xdeadbeef);
    zhandle_t *zk_handle = (zhandle_t *)(&zkh);
    EXPECT_CALL(*zmi, ZookeeperInit(StrEq("127.0.0.1:2181"), _, _, _, _, _))
        .WillOnce(Return(zk_handle));
    EXPECT_CALL(*zmi, ZooState(zk_handle))
        .WillOnce(Return(ZOO_CONNECTED_STATE));
    EXPECT_CALL(*zmi, ZooCreate(zk_handle, StrEq(zk_lock_name),
        StrEq(zk_lock_id), zk_lock_id.length(), _, _, _, _))
        .WillOnce(Return(ZNODEEXISTS))
        .WillOnce(Return(ZNODEEXISTS));
    std::string other_zk_lock_id(zk_lock_id);
    other_zk_lock_id += "-other";
    EXPECT_CALL(*zmi, ZooGet(zk_handle, StrEq(zk_lock_name), _, _, _, _))
        .WillOnce(DoAll(WithArgs<3>(StrCpyToArg0(other_zk_lock_id.c_str())),
            SetArgPointee<4>((int)other_zk_lock_id.length()), Return(ZOK)))
        .WillOnce(DoAll(WithArgs<3>(StrCpyToArg0(zk_lock_id.c_str())),
            SetArgPointee<4>((int)zk_lock_id.length()), Return(ZOK)));
    EXPECT_TRUE(zk_lock.Lock());
    EXPECT_TRUE(cImpl->IsConnected());
    EXPECT_CALL(*zmi, ZooDelete(zk_handle, StrEq(zk_lock_name), _))
        .WillOnce(Return(ZOK));
    EXPECT_CALL(*zmi, ZookeeperClose(zk_handle));
    EXPECT_TRUE(zk_lock.Release());
    EXPECT_FALSE(cImpl->IsConnected());
}

TEST_F(ZookeeperClientTest, ZooCreateRecoverableError) {
    ZookeeperMockInterface *zmi(new ZookeeperMockInterface);
    EXPECT_CALL(*zmi, ZooSetDebugLevel(_));
    impl::ZookeeperClientImpl *cImpl(
        new impl::ZookeeperClientImpl("Test", "127.0.0.1:2181", zmi));
    std::auto_ptr<ZookeeperClient> client(CreateClient(cImpl));
    std::string zk_lock_name("/test-lock");
    ZookeeperLock zk_lock(client.get(), zk_lock_name.c_str());
    std::string zk_lock_id(GetLockId(zk_lock));
    int zkh(0xdeadbeef);
    zhandle_t *zk_handle = (zhandle_t *)(&zkh);
    EXPECT_CALL(*zmi, ZookeeperInit(StrEq("127.0.0.1:2181"), _, _, _, _, _))
        .WillOnce(Return(zk_handle));
    EXPECT_CALL(*zmi, ZooState(zk_handle))
        .WillOnce(Return(ZOO_CONNECTED_STATE));
    EXPECT_CALL(*zmi, ZooCreate(zk_handle, StrEq(zk_lock_name),
        StrEq(zk_lock_id), zk_lock_id.length(), _, _, _, _))
        .WillOnce(Return(ZCONNECTIONLOSS))
        .WillOnce(Return(ZOPERATIONTIMEOUT))
        .WillOnce(Return(ZOK));
    EXPECT_TRUE(zk_lock.Lock());
    EXPECT_TRUE(cImpl->IsConnected());
    EXPECT_CALL(*zmi, ZooDelete(zk_handle, StrEq(zk_lock_name), _))
        .WillOnce(Return(ZOK));
    EXPECT_CALL(*zmi, ZookeeperClose(zk_handle));
    EXPECT_TRUE(zk_lock.Release());
    EXPECT_FALSE(cImpl->IsConnected());
}

TEST_F(ZookeeperClientTest, ZooCreateUnrecoverableError) {
    ZookeeperMockInterface *zmi(new ZookeeperMockInterface);
    EXPECT_CALL(*zmi, ZooSetDebugLevel(_));
    impl::ZookeeperClientImpl *cImpl(
        new impl::ZookeeperClientImpl("Test", "127.0.0.1:2181", zmi));
    std::auto_ptr<ZookeeperClient> client(CreateClient(cImpl));
    std::string zk_lock_name("/test-lock");
    ZookeeperLock zk_lock(client.get(), zk_lock_name.c_str());
    std::string zk_lock_id(GetLockId(zk_lock));
    int zkh(0xdeadbeef);
    zhandle_t *zk_handle = (zhandle_t *)(&zkh);
    EXPECT_CALL(*zmi, ZookeeperInit(StrEq("127.0.0.1:2181"), _, _, _, _, _))
        .WillOnce(Return(zk_handle))
        .WillOnce(Return(zk_handle));
    EXPECT_CALL(*zmi, ZooState(zk_handle))
        .WillOnce(Return(ZOO_CONNECTED_STATE))
        .WillOnce(Return(ZOO_CONNECTED_STATE));
    EXPECT_CALL(*zmi, ZooCreate(zk_handle, StrEq(zk_lock_name),
        StrEq(zk_lock_id), zk_lock_id.length(), _, _, _, _))
        .WillOnce(Return(ZINVALIDSTATE))
        .WillOnce(Return(ZOK));
    EXPECT_CALL(*zmi, ZookeeperClose(zk_handle));
    EXPECT_TRUE(zk_lock.Lock());
    EXPECT_TRUE(cImpl->IsConnected());
    EXPECT_CALL(*zmi, ZooDelete(zk_handle, StrEq(zk_lock_name), _))
        .WillOnce(Return(ZOK));
    EXPECT_CALL(*zmi, ZookeeperClose(zk_handle));
    EXPECT_TRUE(zk_lock.Release());
    EXPECT_FALSE(cImpl->IsConnected());
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
