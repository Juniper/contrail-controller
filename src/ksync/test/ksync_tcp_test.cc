/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <iostream>
#include <fstream>

#include "db/db.h"
#include "db/db_table.h"
#include "db/db_entry.h"
#include "db/db_client.h"
#include "db/db_partition.h"

#include "base/logging.h"
#include "testing/gunit.h"

#include "ksync/ksync_index.h"
#include "ksync/ksync_entry.h"
#include "ksync/ksync_netlink.h"
#include "ksync/ksync_object.h"
#include "io/event_manager.h"
#include "base/test/task_test_util.h"
#include "ksync/ksync_sock.h"

using namespace std;
using namespace boost::asio;
using namespace boost::asio::ip;
using boost::asio::ip::tcp;
uint32_t server_port;

class VlanTable;

VlanTable *vlan_table_;

class TestUT : public ::testing::Test {
public:
    TestUT() { cout << "Creating TestTask" << endl; };
    void TestBody() {};
};

class LocalVrouter {
public:
   LocalVrouter(io_service &io_service) : io_service_(io_service),
   acceptor_(NULL), socket_(NULL) {}
   
   ~LocalVrouter() {}

    void Cleanup() {    
        socket_->close();
        delete socket_;
        socket_ = NULL;

        acceptor_->close();
        delete acceptor_;
        acceptor_ = NULL;
    }
   
   void ProcessData() {
       char data[4096];
       boost::system::error_code error;

       while (1) {
           size_t length = socket_->read_some(buffer(data), error);
           if (error == boost::asio::error::eof) {
               return;
           }
           write(*socket_, buffer(data, length));
       }
   }

   void CreateSocket() {
       acceptor_ = new tcp::acceptor(io_service_, tcp::endpoint(tcp::v4(), 9001));
       socket_ = new tcp::socket(io_service_);

       boost::system::error_code err;
       server_port = acceptor_->local_endpoint(err).port();
       acceptor_->accept(*socket_);
   }

   static void * Run(void *obj) {
       LocalVrouter *objp = reinterpret_cast<LocalVrouter *>(obj);
       objp->CreateSocket();
       objp->ProcessData();
       objp->Cleanup();
       return NULL;
   }

   void Start() {
       assert(pthread_create(&thread_id_, NULL, &Run, this) == 0);
   }

   void Join() {
       assert(pthread_join(thread_id_, NULL) == 0);
   }
 
private:
   boost::asio::io_service &io_service_;
   tcp::acceptor *acceptor_;
   tcp::socket *socket_;
   pthread_t thread_id_;
};

class Vlan : public DBEntry {
public:
    struct VlanKey : public DBRequestKey {
        VlanKey(uint16_t tag) : DBRequestKey(), tag_(tag) { };
        virtual ~VlanKey() { };

        uint16_t tag_;
    };

    Vlan(uint16_t tag) : DBEntry(), tag_(tag) { };
    virtual ~Vlan() { };

    bool IsLess(const DBEntry &rhs) const {
        const Vlan &vlan = static_cast<const Vlan &>(rhs);
        return tag_ < vlan.tag_;
    };
    virtual string ToString() const { return "Vlan"; };
    virtual void SetKey(const DBRequestKey *k) {
        const VlanKey *key = static_cast<const VlanKey *>(k);
        tag_ = key->tag_;
    };

    virtual KeyPtr GetDBRequestKey() const {
        VlanKey *key = new VlanKey(tag_);
        return DBEntryBase::KeyPtr(key);
    };

    uint16_t GetTag() const {return tag_;};
private:
    uint16_t tag_;
    friend class VlanTable;
    DISALLOW_COPY_AND_ASSIGN(Vlan);
};

class VlanTable : public DBTable {
public:
    VlanTable(DB *db, const std::string &name) : DBTable(db, name) { };
    virtual ~VlanTable() { };

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const {
        const Vlan::VlanKey *key = static_cast<const Vlan::VlanKey *>(k);
        Vlan *vlan = new Vlan(key->tag_);
        return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(vlan));
    }

    virtual DBEntry *Add(const DBRequest *req) {
        Vlan::VlanKey *key = static_cast<Vlan::VlanKey *>(req->key.get());
        Vlan *vlan = new Vlan(key->tag_);
        return vlan;
    }

    virtual bool OnChange(DBEntry *entry, const DBRequest *req) {
        return true;
    }

    virtual void Delete(DBEntry *entry, const DBRequest *req) {
        return;
    }

    static VlanTable *CreateTable(DB *db, const string &name) {
        VlanTable *table = new VlanTable(db, name);
        table->Init();
        return table;
    }

private:
    DISALLOW_COPY_AND_ASSIGN(VlanTable);
};

class VlanKSyncEntry : public KSyncNetlinkDBEntry {
public:
    VlanKSyncEntry(const VlanKSyncEntry *entry) : 
        KSyncNetlinkDBEntry(), tag_(entry->tag_) { };

    VlanKSyncEntry(const Vlan *vlan) : 
        KSyncNetlinkDBEntry(), tag_(vlan->GetTag()) { };
    VlanKSyncEntry(const uint16_t tag) :
        KSyncNetlinkDBEntry(), tag_(tag) { };
    virtual ~VlanKSyncEntry() {};

    virtual bool IsLess(const KSyncEntry &rhs) const {
        const VlanKSyncEntry &entry = static_cast<const VlanKSyncEntry &>(rhs);
        return tag_ < entry.tag_;
    }
    virtual std::string ToString() const {return "VLAN";};;
    virtual KSyncEntry *UnresolvedReference() {return NULL;};
    virtual bool Sync(DBEntry *e) {return true;};
    virtual int AddMsg(char *msg, int len) {
        memcpy(msg, &tag_, sizeof(tag_));
        add_count_++;
        return sizeof(tag_);
    };
    virtual int ChangeMsg(char *msg, int len) {
        memcpy(msg, &tag_, sizeof(tag_));
        change_count_++;
        return sizeof(tag_);
    };
    virtual int DeleteMsg(char *msg, int len) {
        memcpy(msg, &tag_, sizeof(tag_));
        del_count_++;
        return sizeof(tag_);
    };
    KSyncDBObject *GetObject();

    uint32_t GetTag() const {return tag_;};
    static void Reset() {
        add_count_ = 0;
        change_count_ = 0;
        del_count_ = 0;
    }

    static int GetAddCount() {return add_count_;};
    static int GetChangeCount() {return change_count_;};
    static int GetDelCount() {return del_count_;};

private:
    uint16_t tag_;
    static int add_count_;
    static int change_count_;
    static int del_count_;
    DISALLOW_COPY_AND_ASSIGN(VlanKSyncEntry);
};
int VlanKSyncEntry::add_count_;
int VlanKSyncEntry::change_count_;
int VlanKSyncEntry::del_count_;

class VlanKSyncObject : public KSyncDBObject {
public:
    VlanKSyncObject(DBTableBase *table) : KSyncDBObject(table) {};

    virtual KSyncEntry *Alloc(const KSyncEntry *entry, uint32_t index) {
        const VlanKSyncEntry *vlan = static_cast<const VlanKSyncEntry *>(entry);
        VlanKSyncEntry *ksync = new VlanKSyncEntry(vlan);
        return static_cast<KSyncEntry *>(ksync);
    };

    virtual KSyncEntry *DBToKSyncEntry(const DBEntry *e) {
        const Vlan *vlan = static_cast<const Vlan *>(e);
        VlanKSyncEntry *key = new VlanKSyncEntry(vlan);
        return static_cast<KSyncEntry *>(key);
    }

    static void Init(VlanTable *table) {
        assert(singleton_ == NULL);
        singleton_ = new VlanKSyncObject(table);
    };

    static void Shutdown() {
        delete singleton_;
        singleton_ = NULL;
    }

    static VlanKSyncObject *GetKSyncObject() { return singleton_; };

private:
    static VlanKSyncObject *singleton_;
    DISALLOW_COPY_AND_ASSIGN(VlanKSyncObject);

};
VlanKSyncObject *VlanKSyncObject::singleton_;

class DBKSyncTcpTest : public ::testing::Test {
protected:
    tbb::atomic<long> adc_notification;
    tbb::atomic<long> del_notification;

public:
    DBKSyncTcpTest() {
        adc_notification = 0;
        del_notification = 0;
    }

    virtual void SetUp() {
        VlanKSyncEntry::Reset();
        itbl = static_cast<VlanTable *>(db_.CreateTable("db.test.vlan.0"));
        tid_ = itbl->Register
            (boost::bind(&DBKSyncTcpTest::DBTestListener, this, _1, _2));
        VlanKSyncObject::Init(itbl);
    }

    virtual void TearDown() {
        itbl->Unregister(tid_);
        db_.RemoveTable(itbl);
        VlanKSyncObject::Shutdown();
        delete itbl;
    }

    void DBTestListener(DBTablePartBase *root, DBEntryBase *entry) {
        Vlan *vlan = static_cast<Vlan *>(entry);
        bool del_notify = vlan->IsDeleted();
        if (del_notify) {
            del_notification++;
        } else {
            adc_notification++;
        }
    }

    DB db_;
    VlanTable *itbl;
    DBTableBase::ListenerId tid_;
    DBTableBase::ListenerId tid_1_;
};

KSyncDBObject *VlanKSyncEntry::GetObject() {
    return VlanKSyncObject::GetKSyncObject();
}

TEST_F(DBKSyncTcpTest, Basic) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new Vlan::VlanKey(10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);

    task_util::WaitForIdle();
    EXPECT_EQ(adc_notification, 1);
    EXPECT_EQ(del_notification, 0);

    EXPECT_EQ(VlanKSyncEntry::GetAddCount(), 1);
    EXPECT_EQ(VlanKSyncEntry::GetDelCount(), 0);

    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(new Vlan::VlanKey(10));
    req.data.reset(NULL);
    itbl->Enqueue(&req);

    task_util::WaitForIdle();
    EXPECT_EQ(adc_notification, 1);
    EXPECT_EQ(del_notification, 1);

    EXPECT_EQ(VlanKSyncEntry::GetAddCount(), 1);
    EXPECT_EQ(VlanKSyncEntry::GetDelCount(), 1);
}

void *asio_poll(void *arg){
    EventManager *evm = reinterpret_cast<EventManager *>(arg);
    evm->Run();
    return NULL;
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    LoggingInit();

    //Create a local vrouter, which listens for conections
    io_service io;
    LocalVrouter lv(io);
    lv.Start();

    pthread_t asio_thread;
    EventManager evm;
    boost::system::error_code ec;
    //Start a asio thread to run
    assert(pthread_create(&asio_thread, NULL, asio_poll, &evm) == 0);

    boost::asio::ip::address ip =
        boost::asio::ip::address::from_string("127.0.0.1", ec);
    KSyncSockTcp::Init(&evm, 1, ip, server_port);

    DB::RegisterFactory("db.test.vlan.0", &VlanTable::CreateTable);
    int ret = RUN_ALL_TESTS();
    evm.Shutdown();
    assert(pthread_join(asio_thread, NULL) == 0);
    lv.Join();

    return ret;
}
