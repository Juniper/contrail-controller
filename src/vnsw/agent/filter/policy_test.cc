/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/string_generator.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>

#include "base/logging.h"
#include "testing/gunit.h"

#include "vnsw/agent/filter/policy.h"
#include "vnsw/agent/filter/policy_config_spec.h"


class MyTest {
  public:
    void TableListener(DBTablePartBase *root, DBEntryBase *entry) {
        std::cout << "Coming here" << std::endl;
        return;
    }
};

namespace {
class PolicyTest : public ::testing::Test {
protected:
};


// Create and delete Policy
TEST_F(PolicyTest, Basic) {
    PolicyConfigSpec policy_c_s1;
    boost::uuids::string_generator gen;

    uuid vpc_id1 = gen("00000000-0000-0000-0000-000000000001");
    uuid policy_id1 = gen("00000000-0000-0000-0000-000000000001");
    policy_c_s1.vpc_id = vpc_id1;
    policy_c_s1.policy_id = policy_id1;
    policy_c_s1.name.clear();
    policy_c_s1.name.append("Contrail Employee SG");
    policy_c_s1.inbound = true;
    policy_c_s1.acl_id = gen("00000000-0000-0000-0000-000000000001");

    DB db;
    PolicyTable::Register();
    DBTableBase *table = db.CreateTable("db.policy.0");
    assert(table);

    PolicyTable *ptable = static_cast<PolicyTable *>(db.FindTable("db.policy.0"));
    assert(ptable);

    DBRequest req;
    req.key.reset(new PolicyKey(policy_id1));

    PolicyData *pd;
    pd = new PolicyData();
    pd->Init(policy_c_s1);
    req.data.reset(pd);
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    table->Enqueue(&req);
    sleep(1);
    EXPECT_TRUE(NULL != ptable->FindActiveEntry(new PolicyKey(policy_id1)));
}


// Modify policy and delete
TEST_F(PolicyTest, ChangeAnEntry) {
    PolicyConfigSpec policy_c_s1;
    boost::uuids::string_generator gen;

    uuid vpc_id1 = gen("00000000-0000-0000-0000-000000000001");
    uuid policy_id1 = gen("00000000-0000-0000-0000-000000000001");
    policy_c_s1.vpc_id = vpc_id1;
    policy_c_s1.policy_id = policy_id1;
    policy_c_s1.name.clear();
    policy_c_s1.name.append("Contrail Employee SG");
    policy_c_s1.inbound = true;
    policy_c_s1.acl_id = gen("00000000-0000-0000-0000-000000000001");

    DB db;
    PolicyTable::Register();
    DBTableBase *table = db.CreateTable("db.policy.0");
    assert(table);

    PolicyTable *ptable = static_cast<PolicyTable *>(db.FindTable("db.policy.0"));
    assert(ptable);

    DBRequest req;
    req.key.reset(new PolicyKey(policy_id1));

    PolicyData *pd;
    pd = new PolicyData();
    pd->Init(policy_c_s1);
    req.data.reset(pd);
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    table->Enqueue(&req);

    sleep(1);
    EXPECT_TRUE(NULL != ptable->FindActiveEntry(new PolicyKey(policy_id1)));

    // Modifing a existing policy
    policy_c_s1.inbound = true;
    policy_c_s1.acl_id = gen("00000000-0000-0000-0000-000000000002");

    Policy *pe = static_cast<Policy *>
        (ptable->FindActiveEntry(new PolicyKey(policy_id1)));
    EXPECT_TRUE(NULL != pe);

    DBRequest req1;
    req1.key.reset(new PolicyKey(policy_id1));
    PolicyData *pd1;
    pd1 = new PolicyData();
    pd1->Init(policy_c_s1);
    req1.data.reset(pd1);
    req1.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    ptable->Enqueue(&req1);
    sleep(1);
    AclPtr ap = pe->FindAcl(true,
                          gen("00000000-0000-0000-0000-000000000002"));
    EXPECT_TRUE(NULL != ap);
    sleep(1);

    DBRequest req2;
    req2.key.reset(new PolicyKey(policy_id1));
    req2.oper = DBRequest::DB_ENTRY_DELETE;
    ptable->Enqueue(&req2);
    sleep(1);
}


} //namespace

int main (int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
