/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_prouter_uve_table_test_h
#define vnsw_agent_prouter_uve_table_test_h

#include <uve/prouter_uve_table_base.h>

class ProuterUveTableTest : public ProuterUveTableBase {
public:
    ProuterUveTableTest(Agent *agent);
    virtual ~ProuterUveTableTest();
    void DispatchProuterMsg(const ProuterData &uve);
    uint32_t send_count() const { return send_count_; }
    uint32_t delete_count() const { return delete_count_; }
    void ClearCount();
    const ProuterData &last_sent_uve() const { return uve_; }
private:
    uint32_t send_count_;
    uint32_t delete_count_;
    ProuterData uve_;
};

#endif // vnsw_agent_prouter_uve_entry_test_h
