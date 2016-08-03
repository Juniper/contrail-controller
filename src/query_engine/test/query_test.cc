/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "query_test.h"

// Message table
void CdbIfMock::initialize_tables()
{
    // MessageTable
    MessageTable = boost::assign::list_of<std::map<std::string, std::string> >
    // Table row #1
    (boost::assign::map_list_of
    // following is straight from tabledump.py output
("UuidKey","6e6c7dcc-800f-4e98-8838-b6e9d9fc21eb") ("Category", "") ("Level", "2147483647") ("MessageTS", "1365791500164230") ("Messagetype", "UveVirtualMachineAgentTrace") ("ModuleId", "VRouterAgent") ("Namespace", "") ("SequenceNum", "298028") ("Source", "a6s41") ("Type", "6") ("VersionSig", "1417765783") 
("Xmlmessage", "<UveVirtualMachineAgentTrace type=\"sandesh\"><data type=\"struct\" identifier=\"1\"><UveVirtualMachineAgent><name type=\"string\" identifier=\"1\" key=\"ObjectVMTable\">debf0699-d1f8-453a-a667-f19afdf88296</name><interface_list type=\"list\" identifier=\"4\"><list type=\"struct\" size=\"1\"><VmInterfaceAgent><name type=\"string\" identifier=\"1\">debf0699-d1f8-453a-a667-f19afdf88296:553e7ec8-7e39-47fe-ad53-bff0a1780af4</name><ip_address type=\"string\" identifier=\"2\">192.168.0.252</ip_address><virtual_network type=\"string\" identifier=\"3\" aggtype=\"listkey\">default-domain:admin:chandan</virtual_network><in_pkts type=\"i64\" identifier=\"5\" aggtype=\"counter\">347951</in_pkts><in_bytes type=\"i64\" identifier=\"6\" aggtype=\"counter\">27335643</in_bytes><out_pkts type=\"i64\" identifier=\"7\" aggtype=\"counter\">480865</out_pkts><out_bytes type=\"i64\" identifier=\"8\" aggtype=\"counter\">201628233</out_bytes></VmInterfaceAgent></list></interface_list></UveVirtualMachineAgent></data></UveVirtualMachineAgentTrace>")
    )
    // End table row #1
    // Table row #2
    (boost::assign::map_list_of
    // following is straight from tabledump.py output
("UuidKey", "6e6c7dcc-800f-4e98-8838-b6e9d9fc21eb") ("Category", "") ("Level", "2147483647") ("MessageTS", "1365791500164230") ("Messagetype", "UveVirtualMachineAgentTrace") ("ModuleId", "VRouterAgent") ("Namespace", "") ("SequenceNum", "265774") ("Source", "a6s41") ("Type", "6") ("VersionSig", "1417765783") ("Xmlmessage", "<UveVirtualMachineAgentTrace type=\"sandesh\"><data type=\"struct\" identifier=\"1\"><UveVirtualMachineAgent><name type=\"string\" identifier=\"1\" key=\"ObjectVMTable\">258d60c5-5e24-4706-9d9f-45be18a7c1a2</name><interface_list type=\"list\" identifier=\"4\"><list type=\"struct\" size=\"1\"><VmInterfaceAgent><name type=\"string\" identifier=\"1\">258d60c5-5e24-4706-9d9f-45be18a7c1a2:da2a19bd-143f-4be2-8b9c-6eee94a38f0a</name><ip_address type=\"string\" identifier=\"2\">192.168.0.253</ip_address><virtual_network type=\"string\" identifier=\"3\" aggtype=\"listkey\">default-domain:admin:chandan</virtual_network><in_pkts type=\"i64\" identifier=\"5\" aggtype=\"counter\">416880</in_pkts><in_bytes type=\"i64\" identifier=\"6\" aggtype=\"counter\">357867668</in_bytes><out_pkts type=\"i64\" identifier=\"7\" aggtype=\"counter\">268869</out_pkts><out_bytes type=\"i64\" identifier=\"8\" aggtype=\"counter\">18995331</out_bytes></VmInterfaceAgent></list></interface_list></UveVirtualMachineAgent></data></UveVirtualMachineAgentTrace>")
    )
    // End table row #2
    // Table row #3
    (boost::assign::map_list_of
    // following is straight from tabledump.py output
("UuidKey", "49509ec8-f1e8-4e57-9a3c-22249b429697") ("Category", "") ("Level", "2147483647") ("MessageTS", "1366045888249603") ("Messagetype", "UveVirtualMachineAgentTrace") ("ModuleId", "VRouterAgent") ("Namespace", "") ("SequenceNum", "379599") ("Source", "a6s41") ("Type", "6") ("VersionSig", "1417765783") ("Xmlmessage", "<UveVirtualMachineAgentTrace type=\"sandesh\"><data type=\"struct\" identifier=\"1\"><UveVirtualMachineAgent><name type=\"string\" identifier=\"1\" key=\"ObjectVMTable\">de8a37e9-458a-4444-be4e-56797bf46018</name><interface_list type=\"list\" identifier=\"4\"><list type=\"struct\" size=\"1\"><VmInterfaceAgent><name type=\"string\" identifier=\"1\">de8a37e9-458a-4444-be4e-56797bf46018:15f120d1-4469-470d-bf0c-bb30607ad96d</name><ip_address type=\"string\" identifier=\"2\">192.168.0.251</ip_address><virtual_network type=\"string\" identifier=\"3\" aggtype=\"listkey\">default-domain:admin:chandan</virtual_network><in_pkts type=\"i64\" identifier=\"5\" aggtype=\"counter\">180570</in_pkts><in_bytes type=\"i64\" identifier=\"6\" aggtype=\"counter\">16909512</in_bytes><out_pkts type=\"i64\" identifier=\"7\" aggtype=\"counter\">195174</out_pkts><out_bytes type=\"i64\" identifier=\"8\" aggtype=\"counter\">181445024</out_bytes></VmInterfaceAgent></list></interface_list></UveVirtualMachineAgent></data></UveVirtualMachineAgentTrace>")
    )
    // End table row #3
    // Table row #4
    (boost::assign::map_list_of
    // following is straight from tabledump.py output
("UuidKey", "378ded22-b727-4bfc-8049-4278da18546d") ("Category", "") ("Level", "2147483647") ("MessageTS", "1366050456842541") ("Messagetype", "UveVirtualMachineAgentTrace") ("ModuleId", "VRouterAgent") ("Namespace", "") ("SequenceNum", "386450") ("Source", "a6s41") ("Type", "6") ("VersionSig", "1417765783") ("Xmlmessage", "<UveVirtualMachineAgentTrace type=\"sandesh\"><data type=\"struct\" identifier=\"1\"><UveVirtualMachineAgent><name type=\"string\" identifier=\"1\" key=\"ObjectVMTable\">debf0699-d1f8-453a-a667-f19afdf88296</name><interface_list type=\"list\" identifier=\"4\"><list type=\"struct\" size=\"1\"><VmInterfaceAgent><name type=\"string\" identifier=\"1\">debf0699-d1f8-453a-a667-f19afdf88296:553e7ec8-7e39-47fe-ad53-bff0a1780af4</name><ip_address type=\"string\" identifier=\"2\">192.168.0.252</ip_address><virtual_network type=\"string\" identifier=\"3\" aggtype=\"listkey\">default-domain:admin:chandan</virtual_network><in_pkts type=\"i64\" identifier=\"5\" aggtype=\"counter\">408733</in_pkts><in_bytes type=\"i64\" identifier=\"6\" aggtype=\"counter\">33191544</in_bytes><out_pkts type=\"i64\" identifier=\"7\" aggtype=\"counter\">541617</out_pkts><out_bytes type=\"i64\" identifier=\"8\" aggtype=\"counter\">207481994</out_bytes></VmInterfaceAgent></list></interface_list></UveVirtualMachineAgent></data></UveVirtualMachineAgentTrace>")
    )
    // End table row #4
    // Table row #5
    (boost::assign::map_list_of
    // following is straight from tabledump.py output
("UuidKey", "96069c43-63c4-460f-95b9-87a50c5ee685") ("Category", "") ("Level", "2147483647") ("MessageTS", "1366034518750909") ("Messagetype", "UveVirtualNetworkAgentTrace") ("ModuleId", "VRouterAgent") ("Namespace", "") ("SequenceNum", "272402") ("Source", "a6s41") ("Type", "6") ("VersionSig", "-1181885309") ("Xmlmessage", "<UveVirtualNetworkAgentTrace type=\"sandesh\"><data type=\"struct\" identifier=\"1\"><UveVirtualNetworkAgent><name type=\"string\" identifier=\"1\" key=\"ObjectVNTable\">__UNKNOWN__</name><in_stats type=\"list\" identifier=\"9\" aggtype=\"append\"><list type=\"struct\" size=\"1\"><UveInterVnStats><other_vn type=\"string\" identifier=\"1\" aggtype=\"listkey\">default-domain:admin:chandan</other_vn><tpkts type=\"i64\" identifier=\"2\">291</tpkts><bytes type=\"i64\" identifier=\"3\">18036</bytes></UveInterVnStats></list></in_stats></UveVirtualNetworkAgent></data></UveVirtualNetworkAgentTrace>")
    )
    // End table row #5
    // Table row #6
    (boost::assign::map_list_of
    // following is straight from tabledump.py output
("UuidKey", "f2994085-d98c-4047-8876-4f435fd6a7a0") ("Category", "") ("Level", "2147483647") ("MessageTS", "1365966968177813") ("Messagetype", "UveVirtualNetworkAgentTrace") ("ModuleId", "VRouterAgent") ("Namespace", "") ("SequenceNum", "204861") ("Source", "a6s41") ("Type", "6") ("VersionSig", "-1181885309") ("Xmlmessage", "<UveVirtualNetworkAgentTrace type=\"sandesh\"><data type=\"struct\" identifier=\"1\"><UveVirtualNetworkAgent><name type=\"string\" identifier=\"1\" key=\"ObjectVNTable\">default-domain:admin:chandan</name><interface_list type=\"list\" identifier=\"4\" aggtype=\"union\"><list type=\"string\" size=\"3\"><element>tap553e7ec8-7e</element><element>tapda2a19bd-14</element><element>tap15f120d1-44</element></list></interface_list><in_tpkts type=\"i64\" identifier=\"5\" aggtype=\"counter\">841144</in_tpkts><in_bytes type=\"i64\" identifier=\"6\" aggtype=\"counter\">391938964</in_bytes><out_tpkts type=\"i64\" identifier=\"7\" aggtype=\"counter\">840681</out_tpkts><out_bytes type=\"i64\" identifier=\"8\" aggtype=\"counter\">391896869</out_bytes><in_stats type=\"list\" identifier=\"9\" aggtype=\"append\"><list type=\"struct\" size=\"1\"><UveInterVnStats><other_vn type=\"string\" identifier=\"1\" aggtype=\"listkey\">default-domain:admin:chandan</other_vn><tpkts type=\"i64\" identifier=\"2\">826863</tpkts><bytes type=\"i64\" identifier=\"3\">379120878</bytes></UveInterVnStats></list></in_stats><out_stats type=\"list\" identifier=\"10\" aggtype=\"append\"><list type=\"struct\" size=\"2\"><UveInterVnStats><other_vn type=\"string\" identifier=\"1\" aggtype=\"listkey\">__UNKNOWN__</other_vn><tpkts type=\"i64\" identifier=\"2\">201</tpkts><bytes type=\"i64\" identifier=\"3\">12636</bytes></UveInterVnStats><UveInterVnStats><other_vn type=\"string\" identifier=\"1\" aggtype=\"listkey\">default-domain:admin:chandan</other_vn><tpkts type=\"i64\" identifier=\"2\">826863</tpkts><bytes type=\"i64\" identifier=\"3\">379120878</bytes></UveInterVnStats></list></out_stats><acl type=\"string\" identifier=\"12\"></acl></UveVirtualNetworkAgent></data></UveVirtualNetworkAgentTrace>")
    )
    // End table row #6
 ;
    // End MessageTable
}

bool CdbIfMock::Db_Init() {return true;}

bool CdbIfMock::Db_AddSetTablespace(const std::string& tablespace, const std::string& replication_factor)
{ 
    QE_ASSERT(tablespace == g_viz_constants.COLLECTOR_KEYSPACE);
    return true;
}

bool CdbIfMock::Db_GetMultiRow(GenDb::ColListVec *col_list,
        const std::string &cfname,
        const std::vector<GenDb::DbDataValueVec> &v_rowkey)
{
    {
        // Lookup in MessageTable
        for (unsigned int i = 0; i < v_rowkey.size(); i++)
        {
	    GenDb::DbDataValueVec u;
            std::stringstream ss; 
            for (unsigned int k = 0; k < v_rowkey[i].size(); k++) {
		ss << boost::get<boost::uuids::uuid>(v_rowkey[i][k]);
	    }

            for (unsigned int j = 0; j < MessageTable.size(); j++)
            {
                std::map<std::string, std::string>::iterator it;
                it = MessageTable[j].find(g_viz_constants.UUID_KEY);
                if (it == MessageTable[j].end())
                    break;

                if (ss.str() == it->second)
                {
                    // matching table row
                    std::map<std::string, std::string>::iterator iter;
                    for (iter = MessageTable[j].begin();
                            iter != MessageTable[j].end(); iter++)
                    {
			GenDb::ColList *col = new GenDb::ColList;
			col->rowkey_ = v_rowkey[i];
			col->cfname_ = cfname;

			GenDb::NewColVec colvec;

                        if (iter->first == g_viz_constants.UUID_KEY) {
			    GenDb::NewCol *newcol = new GenDb::NewCol(iter->first, iter->second, 0);
			    col->columns_.push_back(newcol);
                            col_list->push_back(col);
                        } else if ((iter->first == g_viz_constants.SOURCE) ||
                            (iter->first == g_viz_constants.SOURCE) ||
                            (iter->first == g_viz_constants.NAMESPACE) ||
                            (iter->first == g_viz_constants.MODULE) ||
                            (iter->first == g_viz_constants.CONTEXT) ||
                            (iter->first == g_viz_constants.CATEGORY) ||
                            (iter->first == g_viz_constants.MESSAGE_TYPE) ||
                            (iter->first == g_viz_constants.DATA)) {
			    GenDb::NewCol *newcol = new GenDb::NewCol(iter->first, iter->second, 0);
			    col->columns_.push_back(newcol);
                        } else if (iter->first == g_viz_constants.TIMESTAMP) {
                            int64_t val;
                            stringToInteger(iter->second, val);
			    GenDb::NewCol *newcol = new GenDb::NewCol(iter->first, (const char *)&val, 0);
			    col->columns_.push_back(newcol);
                        } else if ((iter->first == g_viz_constants.LEVEL) ||
                            (iter->first == g_viz_constants.SEQUENCE_NUM) ||
                            (iter->first == g_viz_constants.VERSION) ||
                            (iter->first == g_viz_constants.SANDESH_TYPE)) {
                            int32_t val;
                            stringToInteger(iter->second, val);
			    GenDb::NewCol *newcol = new GenDb::NewCol(iter->first, (const char*)&val, 0);
			    col->columns_.push_back(newcol);
                        } else {
			    GenDb::NewCol *newcol = new GenDb::NewCol(iter->first, iter->second, 0);
			    col->columns_.push_back(newcol);
                        }
                        col_list->push_back(col);
                    }
                } 
            } // finished iterating over all table rows
        } // finished iterating over all keys 
    } // End Message Table Simulation

    return true;
}


// actual google test classes
#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/assign/list_of.hpp>
#include "testing/gunit.h"
#include "base/logging.h"
#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"

using ::testing::Return;
using ::testing::Field;
using ::testing::AnyOf;
using ::testing::AnyNumber;
using ::testing::_;
using ::testing::Eq;
using ::testing::ElementsAre;

class AnalyticsQueryTest: public ::testing::Test {
public:
    AnalyticsQueryTest() :
        dbif_mock_(new CdbIfMock(&evm_)) { }

    ~AnalyticsQueryTest() {
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

    CdbIfMock *dbif_mock() {
        return dbif_mock_;
    }

    CdbIfMock *dbif_mock_;
private:
    void QueryErrorHandlerFn() {
        assert(0);
    }

    EventManager evm_;
};

TEST_F(AnalyticsQueryTest, MessageTableTest) {
    // Create the query first
    std::string qid("TEST-QUERY");
    std::map<std::string, std::string> json_api_data;
    json_api_data.insert(std::pair<std::string, std::string>(
                "table", "\"StatTable.SomeStatTable\""
    ));

    json_api_data.insert(std::pair<std::string, std::string>(
    "start_time", "1365791500164229"
    ));
    json_api_data.insert(std::pair<std::string, std::string>(
    "end_time",   "1365997500164232" 
    ));
    json_api_data.insert(std::pair<std::string, std::string>(
    "select_fields", "[\"ModuleId\", \"Source\", \"Level\", \"Messagetype\"]"
    ));
    json_api_data.insert(std::pair<std::string, std::string>(
    "limit", "1"
    ));
    TtlMap ttlmap_;
    std::vector<query_result_unit_t> where_info;
    query_result_unit_t query_result;
    boost::uuids::uuid uuid = StringToUuid("6e6c7dcc-800f-4e98-8838-b6e9d9fc21eb");
    query_result.info.push_back("\{\"Messagetypess\":\"*\"}");
    query_result.info.push_back(uuid);
    where_info.push_back(query_result);
    query_result_unit_t query_result2;
    query_result2.info.push_back("\{\"Sourcess\":\"*\"}");
    uuid = StringToUuid("some-random-string");
    query_result2.info.push_back(uuid);
    where_info.push_back(query_result2);
    AnalyticsQuery *q = new AnalyticsQuery(qid, (boost::shared_ptr<GenDb::GenDbIf>)dbif_mock_, json_api_data, -1, &where_info, ttlmap_, 0, 1);
    
    EXPECT_EQ(QUERY_SUCCESS, q->process_query()); // query was parsed and successful

    EXPECT_EQ(1, q->final_mresult->size()); // one row as result due to limit of 1
    delete q;
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

