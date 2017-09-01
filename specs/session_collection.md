
# 1. Introduction
We are evolving the Flow Collection infrastructure in Contrail to implement Security Policy, and to satisfy the requirements of EN-196, so it’s useful to consider some design goals and background for them.

# 2. Problem statement
The current collection focused on individual flows, which we are considering moving to bi-directional sessions instead, which consist of both the forward flow and the reverse flow. From a visibility and security standpoint, sessions are the right abstraction. From an implementation standpoint, it allows us to store half the number of records. We also have the opportunity to do better pre-aggregation at collection time, which can reduce the number of records even further, and make queries and stream processing easier and more efficient.
consider the use case below:

![Image of session-collection](images/session-collection1.png)
Consider the session shown here. There is client on vRouter1, which initiates a session to a server on vRouter2, which then responds. We identify this session according to the packet header from the first packet sent from the client to the server: CVN = VN1, CIP = IP1, CPort = p1, SVN = SN2, SIP = IP2, SPort = p2, Protocol = X. Both vRouters are able to use the same identity for the session based on the forward flow seen by them. But vRouter1 will record the client part of the session, whereas vRouter2 will record the server part.

Another usecase is detecting anomaly in the sessions. New config objects will be added, to indicate VMI's of interest along with anamoly detection algorithm. This can then be used to detect any abnormalities in session creation.

# 3. Proposed solution

We can always record traffic information for both the client and server part based on the 7-tuple, but some elements of this 7-tuple are more important for analysis than others. We need to design for more efficient queries for more important elements, and we can make less efficient queries for others. The maximum efficiency is gained by pre-aggregating when the information is recorded, so the number of records is reduced.  (e.g. for the sake of efficiency, we currently record InterVN stats explicitly by pre-aggregating across all elements of the 7-tuple except the VNs, even though we could have got the same information from flow records).

Notice that the CPort is usually not interesting for analysis (on both the Client part and the Server part), because it is an ephemeral port. In fact, when use fat-flows, it is always recorded as 0.  (by contrast, both Client and Server session parts find SPort very interesting, because it usually represents the service being offered or consumed – 80 in the case of HTTP). So, we can pre-aggregate by CPort.

Now let discuss the Client Part and the Server Part separately, starting with the Server part. The CIP and SIP fields are candidates for pre-aggregation also – they are less important than CVN/SVN. But there isn’t much to be gained on the Server part by pre-aggregating on a per Server-IP basis – a single vRouter will have a handful of Server-IPs anyway.

So, we pre-aggregate based on CIP and CPORT, and send information per combination of CVN-SVN-SIP-Protocol-SPORT.

This is what is needed for analyzing flows on the 7-tuple basis. But, we are also adding an enhanced security framework to manage connectivity between workloads (i.e. VMIs). Each VMI is “tagged” with the attributes of Deployment, App, Tier and Site. The user specifies security policies for VMIs in terms of values of these tags. We will need to analyze traffic flow between groups of VMI, where groups are categorized according to one or more values of these tags. These tags will also be communicated in routes, so that the vRouter has tag information about remote endpoints that it’s local VMIs are talking to. However, in the routes, these tags are IDs, not strings. When sending remote tags to analytics, vRouter agent should send names for tags in its own project and for global tags. Other tags may be sent as IDs.

To give us the ability to analyze traffic between these VMI groups, as well as security policies, as well as CVN-SVN-SIP-Protocol-SPORT, we are proposing that the current Flow message be replaced by a “SessionEndpoint” message. This message has these parts:

1.	The identity of the SessionEndpoint, which is a combination of:
    *	VMI. Given the local VMI, its tags and VNs are known too.
    *	Security Policy and Security Rule.
    *	Route attributes for the remote endpoint – tags and VN. For North-South routes and ECMP routes, we can use the route prefix as well.
2.	A SessionAggregate map, keyed by SIP-Protocol-SPort (for the Client part, it will be CIP-Protocol-SPort). The values in this map consist of the following:
    *	Session map, which contains the actual individual sessions for the aggregate, keyed by CPort and CIP. (for the Client part, it will be CPort and SIP). A given session can be marked as logged, sampled or both. The sampling works in similar was as the current Flow Sampling, but we need to do “Session Sampling” instead of “Flow Sampling”. For any given session, we need to either send both the forward-flow and reverse-flow, or send neither of them. The thresholding, upscaling and probability check actions can be based on either forward flow or reverse flow metrics, but the actions will need to be taken on both the flows.
    *	SessionAggregate counts - bytes/packets/flows. These counts should correspond exactly with the corresponding session map, and the sampled values in the session entries. Traffic analysis within and across Security Policies will use these session aggregate counts, instead of per-session counts.

As had been discussed, Session data can belong to either Sampled or Logged Flows. The destinations to which the SessionAggregates will be sent are configurable. Collector, local log and syslog are the different destinations that will be supported for the SessionAggregates


## 3.1 Alternatives considered


## 3.2 API schema changes


## 3.3 User workflow impact


## 3.4 UI changes


## 3.5 Notification impact

The FlowLogData that is being sent currently is going to be replaced by SessionEndpointData. Also an additional structure provide statistics about the security tags is also going to be provided. Here is a description of each of the structures.
```
    objectlog sandesh EndpointSecurityStats {
      1: string       name (key="ObjectVMITable")
     50: string       app
     51: string       tier
     52: string       site
     53: string       deployment
     54: string       vn
    
     // The key of this map is Security-Rule
      55: map<string, EndpointStats> eps (tags=”name,vn,app,tier,site,deployment,.__key”)
    }
    
    struct EndpointStats {
     10: list<SecurityPolicySessionStats> client (tags=””)
     11: list<SecurityPolicySessionStats> server (tags=””)
    }
    
    struct SecurityPolicySessionStats {
      1: string       remote_app_id
      2: string       remote_tier_id
      3: string       remote_site_id
      4: string       remote_deployment_id
      5: string       remote_prefix // use when app/tier/site/deployment is missing
      6: string       remote_vn
      7: u64 hits
      8: u64 tx_bytes;
      9: u64 rx_bytes;
     10: u64 tx_pkts;
     11: u64 rx_pkts;
    }
```
Next is the SessionEndpointData object reported by agent periodically:
```
    FlowLog SessionEndpoint {
        1: string vmi;
        2: string vn;
        3: optional string deployment;
        4: optional string tier;
        5: optional string application;
        6: optional string site;
        7: optional set<string> labels;
        8: optional string remote_deployment;
        9: optional string remote_tier;
       10: optional string remote_application;
       11: optional string remote_site;
       12: optional set<string> remote_labels;
       13: string remote_vn;
       14: bool is_client_session;
       15: bool is_si;
       16: optional string remote_prefix;
       17: ipaddr vrouter_ip;
       /**
        *  @display_name: key is local session end point defined as
        *  (protocol-port, ip address) and val is remote session end point
        */
       18: map<SessionIpPortProtocol, SessionAggInfo> sess_agg_info;
    };

    /**
    * @description:This structure contains a map of all the remote
    * session end points connected to this session. key uniquely
    * identifies the remote session end point and value is the
    * traffic info, security info, to that end point
    */
    struct SessionAggInfo {
    	1: optional i64 sampled_tx_bytes;
        2: optional i64 sampled_tx_pkts;
        3: optional i64 sampled_rx_bytes;
        4: optional i64 sampled_rx_pkts;
        5: optional i64 logged_tx_bytes;
        6: optional i64 logged_tx_pkts;
        7: optional i64 logged_rx_bytes;
        8: optional i64 logged_rx_pkts;
        9: map<SessionIpPort, SessionInfo> sessionMap;
    };
   
    struct SessionFlowInfo {
        1: optional i64 sampled_bytes;
        2: optional i64 sampled_pkts;
        3: optional i64 logged_bytes;
        4: optional i64 logged_pkts;
        5: optional uuid_t flow_uuid;
        6: optional i16 tcp_flags;
        7: optional i64 setup_time;
        8: optional i64 teardown_time;
        9: optional i64 teardown_bytes;
       10: optional i64 teardown_pkts;
       11: optional string action;
       12: optional uuid_t sg_rule_uuid;
       13: optional uuid_t nw_ace_uuid;
       14: optional u16 underlay_source_port;
       15: optional u16 drop_reason;
    }

    struct SessionInfo {
        1: SessionFlowInfo forward_flow_info;
        2: SessionFlowInfo reverse_flow_info;
        3: optional string vm;
        4: optional ipaddr other_vrouter_ip;
        5: optional u16 underlay_proto;
    }
```
# 4. Implementation
## 4.1 Work items

### 4.1.1 Agent:
The sampling algorithm used should not sample the session instead of the individual flow. The agent should also aggregate the sessions that belong to the same server port before sending it. The sandesh library in the agent should do a syslog of each of the sessions inside the aggregate message that are marked for security logging.
### 4.1.2 Analytics:
New cassandra tables have to be created that can index the interesting fields in theSessionEndpointData message that it receives. Queryengine should also be modified so that data can be fetched with this new database schema.

New Database Schema will be as below:
```
const map<string, table_schema> _VIZD_SESSION_TABLE_SCHEMA = {
    SESSION_TABLE : {
        'columns' : [
            {'name' : 'key', 'datatype': Gendb.DbDataType.Unsigned32Type },
            {'name' : 'key2', 'datatype': Gendb.DbDataType.Unsigned8Type },
            {'name' : 'key3', 'datatype': Gendb.DbDataType.Unsigned8Type },
            {'name' : 'key4', 'datatype': Gendb.DbDataType.Unsigned8Type },
            {'name' : 'column1',
             'datatype': Gendb.DbDataType.Unsigned16Type, 'clustering': true },
            {'name' : 'column2',
             'datatype': Gendb.DbDataType.Unsigned16Type, 'clustering': true },
            {'name' : 'column3',
             'datatype': Gendb.DbDataType.Unsigned32Type, 'clustering': true },
            {'name' : 'column4',
             'datatype': Gendb.DbDataType.LexicalUUIDType, 'clustering': true},
            {'name' : 'column5', 'datatype': Gendb.DbDataType.UTF8Type,
             'index_type' : ColIndexType.CUSTOM_INDEX,
             'index_mode' : ColIndexMode.PREFIX },
            {'name' : 'column6', 'datatype': Gendb.DbDataType.UTF8Type,
             'index_type' : ColIndexType.CUSTOM_INDEX,
             'index_mode' : ColIndexMode.PREFIX },
            {'name' : 'column7', 'datatype': Gendb.DbDataType.UTF8Type,
             'index_type' : ColIndexType.CUSTOM_INDEX,
             'index_mode' : ColIndexMode.PREFIX },
            {'name' : 'column8', 'datatype': Gendb.DbDataType.UTF8Type,
             'index_type' : ColIndexType.CUSTOM_INDEX,
             'index_mode' : ColIndexMode.PREFIX },
            {'name' : 'column9', 'datatype': Gendb.DbDataType.UTF8Type},
            {'name' : 'column10', 'datatype': Gendb.DbDataType.UTF8Type,
             'index_type' : ColIndexType.CUSTOM_INDEX,
             'index_mode' : ColIndexMode.PREFIX },
            {'name' : 'column11', 'datatype': Gendb.DbDataType.UTF8Type,
             'index_type' : ColIndexType.CUSTOM_INDEX,
             'index_mode' : ColIndexMode.PREFIX },
            {'name' : 'column12', 'datatype': Gendb.DbDataType.UTF8Type,
             'index_type' : ColIndexType.CUSTOM_INDEX,
             'index_mode' : ColIndexMode.PREFIX },
            {'name' : 'column13', 'datatype': Gendb.DbDataType.UTF8Type,
             'index_type' : ColIndexType.CUSTOM_INDEX,
             'index_mode' : ColIndexMode.PREFIX },
            {'name' : 'column14', 'datatype': Gendb.DbDataType.UTF8Type },
            {'name' : 'column15', 'datatype': Gendb.DbDataType.UTF8Type,
             'index_type' : ColIndexType.CUSTOM_INDEX,
             'index_mode' : ColIndexMode.PREFIX },
            {'name' : 'column16', 'datatype': Gendb.DbDataType.UTF8Type,
             'index_type' : ColIndexType.CUSTOM_INDEX, 
             'index_mode' : ColIndexMode.PREFIX },
            {'name' : 'column17', 'datatype': Gendb.DbDataType.UTF8Type,
             'index_type' : ColIndexType.CUSTOM_INDEX,
             'index_mode' : ColIndexMode.PREFIX },
            {'name' : 'column18', 'datatype': Gendb.DbDataType.UTF8Type,
             'index_type' : ColIndexType.CUSTOM_INDEX,
             'index_mode' : ColIndexMode.PREFIX },
            {'name' : 'column19', 'datatype': Gendb.DbDataType.UTF8Type,
             'index_type' : ColIndexType.CUSTOM_INDEX,
             'index_mode' : ColIndexMode.PREFIX },
            {'name' : 'column20', 'datatype': Gendb.DbDataType.UTF8Type,
             'index_type' : ColIndexType.CUSTOM_INDEX,
             'index_mode' : ColIndexMode.PREFIX },
            {'name' : 'column21', 'datatype': Gendb.DbDataType.AsciiType },
            {'name' : 'column22', 'datatype': Gendb.DbDataType.InetType },
            {'name' : 'column23', 'datatype': Gendb.DbDataType.Unsigned64Type },
            {'name' : 'column24', 'datatype': Gendb.DbDataType.Unsigned64Type },
            {'name' : 'column25', 'datatype': Gendb.DbDataType.Unsigned64Type },
            {'name' : 'column26', 'datatype': Gendb.DbDataType.Unsigned64Type },
            {'name' : 'column27', 'datatype': Gendb.DbDataType.Unsigned64Type },
            {'name' : 'column28', 'datatype': Gendb.DbDataType.Unsigned64Type },
            {'name' : 'column29', 'datatype': Gendb.DbDataType.Unsigned64Type },
            {'name' : 'column30', 'datatype': Gendb.DbDataType.Unsigned64Type },
            {'name' : 'value', 'datatype': Gendb.DbDataType.UTF8Type },
        ],
        'column_to_query_column' : {
            'column0'  : 'T2',
            'column1'  : 'partition_number',
            'column2'  : 'is_service_instance',
            'column3'  : 'session_type',
            'column4'  : 'protocol',
            'column5'  : 'server_port',
            'column6'  : 'T1',
            'column7'  : 'uuid',
            'column8'  : 'deployment',
            'column9'  : 'tier',
            'column10' : 'application',
            'column11' : 'site',
            'column12' : 'labels',
            'column13' : 'remote_deployment',
            'column14' : 'remote_tier',
            'column15' : 'remote_application',
            'column16' : 'remote_site',
            'column17' : 'remote_labels',
            'column18' : 'remote_prefix',
            'column19' : 'security_policy_rule',
            'column20' : 'vmi',
            'column21' : 'local_ip',
            'column22' : 'vn',
            'column23' : 'remote_vn',
            'column24' : 'vrouter',
            'column25' : 'vrouter_ip',
            'column26' : 'forward_sampled_bytes',
            'column27' : 'forward_sampled_pkts',
            'column28' : 'forward_logged_bytes',
            'column29' : 'forward_logged_pkts',
            'column30' : 'reverse_sampled_bytes',
            'column31' : 'reverse_sampled_pkts',
            'column32' : 'reverse_logged_bytes',
            'column33' : 'reverse_logged_pkts',
        },
        'index_column_to_column' : {
            'deployment'           : 'column5',
            'tier'                 : 'column6',
            'application'          : 'column7',
            'site'                 : 'column8',
            'remote_deployment'    : 'column10',
            'remote_tier'          : 'column11',
            'remote_application'   : 'column12',
            'remote_site'          : 'column13',
            'remote_prefix'        : 'column15',
            'security_policy_rule' : 'column16',
            'vmi'                  : 'column17',
            'ip'                   : 'column18',
            'vn'                   : 'column19',
            'remote_vn'            : 'column20',
        }
    }
}
```
We will have two Virtual Tables for Querying:
    *    SessionRecordTable: To get information about all the unique Sessions in given time period
    *    SessionSeriesTable: To get timeseries data for session
The Virtual Table Schemas will be as below:
```
1. SessionRecordTable
   {
         'name' : 'SessionRecordTable,
         'schema' : {
             'type' : 'SESSION',
             'columns' :  [
               { 'name' : 'session_type', 'datatype' : 'string', 'index' : true, 'requiredness' : 'mandatory', 'choices' : ['server','client'] },
               { 'name' : 'is_service_instance', 'datatype' : 'int', 'index' : true, 'default' : false },
               { 'name' : 'vmi', 'datatype' : 'string', 'index' : true },
               { 'name' : 'vn', 'datatype' : 'string', 'index' : true },
               { 'name' : 'deployment', 'datatype' : 'string', 'index' : true },
               { 'name' : 'application', 'datatype' : 'string', 'index' : true },
               { 'name' : 'tier', 'datatype' : 'string', 'index' : true },
               { 'name' : 'site', 'datatype' : 'string', 'index' : true },
               { 'name' : 'labels', 'datatype' : 'string', 'index' : false },
               { 'name' : 'remote_deployment', 'datatype' : 'string', 'index' : true },
               { 'name' : 'remote_application', 'datatype' : 'string', 'index' : true },
               { 'name' : 'remote_tier', 'datatype' : 'string', 'index' : true },
               { 'name' : 'remote_site', 'datatype' : 'string', 'index' : true },
               { 'name' : 'remote_labels', 'datatype' : 'string', 'index' : false },
               { 'name' : 'remote_vn', 'datatype' : 'string', 'index' : true },
               { 'name' : 'remote_prefix', 'datatype' : 'string', 'index' : false },
               { 'name' : 'security_policy_rule', 'datatype' : 'string', 'index' : true },

               // IP, Protocol, Port indexes for session
               { 'name' : 'local_ip', 'datatype' : 'ipaddr', 'index' : true },
               { 'name' : 'protocol', 'datatype' : 'int', 'index' : true },
               { 'name' : 'server_port', 'datatype' : 'int', 'index' : true },

               { 'name' : 'remote_ip', 'datatype' : 'ipaddr', 'index' : false },
               { 'name' : 'client_port', 'datatype' : 'int', 'index' : false },

               { 'name' : 'vm', 'datatype' : 'string', 'index' : false },                
               { 'name' : 'vrouter', 'datatype' : 'string', 'index' : false },
               { 'name' : 'vrouter_ip', 'datatype' : 'ipaddr', 'index' : false },
               { 'name' : 'other_vrouter_ip', 'datatype' : 'ipaddr', 'index' : false },
               { 'name' : 'underlay_proto', 'datatype' : 'int', 'index' : false },
 
               // Flow based fields
               { 'name' : 'forward_flow_uuid', 'datatype' : 'uuid_t', 'index' : false },
               { 'name' : 'forward_setup_time', 'datatype' : 'long', 'index' : false },
               { 'name' : 'forward_teardown_time', 'datatype' : 'long', 'index' : false },
               { 'name' : 'forward_action', 'datatype' : 'string', 'index' : false },
               { 'name' : 'forward_sg_rule_uuid', 'datatype' : 'uuid', 'index' : false },
               { 'name' : 'forward_nw_ace_uuid', 'datatype' : 'uuid', 'index' : false },              
               { 'name' : 'forward_underlay_source_port', 'datatype' : 'int', 'index' : false },
               { 'name' : 'forward_drop_reason', 'datatype' : 'string', 'index' : false },

               { 'name' : 'reverse_flow_uuid', 'datatype' : 'uuid_t', 'index' : false, 'requiredness' : 'mandatory' },
               { 'name' : 'reverse_setup_time', 'datatype' : 'long', 'index' : false },
               { 'name' : 'reverse_teardown_time', 'datatype' : 'long', 'index' : false },
               { 'name' : 'reverse_action', 'datatype' : 'string', 'index' : false },
               { 'name' : 'reverse_sg_rule_uuid', 'datatype' : 'uuid', 'index' : false },
               { 'name' : 'reverse_nw_ace_uuid', 'datatype' : 'uuid', 'index' : false },              
               { 'name' : 'reverse_underlay_source_port', 'datatype' : 'int', 'index' : false },
               { 'name' : 'reverse_drop_reason', 'datatype' : 'string', 'index' : false },

               { 'name' : 'forward_teardown_bytes', 'datatype' : 'long', 'index' : false },
               { 'name' : 'forward_teardown_packets', 'datatype' : 'long', 'index' : false },

               { 'name' : 'reverse_teardown_bytes', 'datatype' : 'long', 'index' : false },
               { 'name' : 'reverse_teardown_packets', 'datatype' : 'long', 'index' : false },
               ]
         },
         'columnvalues' : [ ],
    }

2. SessionSeriesTable
   {
         'name' : 'SessionSeriesTable,
         'schema' : {
             'type' : 'SESSION',
             'columns' :  [
               { 'name' : 'session_type', 'datatype' : 'string', 'index' : true, 'requiredness' : 'mandatory', 'choices' : ['server','client'] },
               { 'name' : 'is_service_instance', 'datatype' : 'int', 'index' : true, 'default' : false },
               { 'name' : 'vmi', 'datatype' : 'string', 'index' : true },
               { 'name' : 'vn', 'datatype' : 'string', 'index' : true },
               { 'name' : 'deployment', 'datatype' : 'string', 'index' : true },
               { 'name' : 'application', 'datatype' : 'string', 'index' : true },
               { 'name' : 'tier', 'datatype' : 'string', 'index' : true },
               { 'name' : 'site', 'datatype' : 'string', 'index' : true },
               { 'name' : 'labels', 'datatype' : 'string', 'index' : false },
               { 'name' : 'remote_deployment', 'datatype' : 'string', 'index' : true },
               { 'name' : 'remote_application', 'datatype' : 'string', 'index' : true },
               { 'name' : 'remote_tier', 'datatype' : 'string', 'index' : true },
               { 'name' : 'remote_site', 'datatype' : 'string', 'index' : true },
               { 'name' : 'remote_labels', 'datatype' : 'string', 'index' : false },
               { 'name' : 'remote_vn', 'datatype' : 'string', 'index' : true },
               { 'name' : 'remote_prefix', 'datatype' : 'string', 'index' : true },
               { 'name' : 'security_policy_rule', 'datatype' : 'string', 'index' : true },

               // IP, Protocol, Port indexes for session
               { 'name' : 'local_ip', 'datatype' : 'ipaddr', 'index' : true },
               { 'name' : 'protocol', 'datatype' : 'int', 'index' : true },
               { 'name' : 'server_port', 'datatype' : 'int', 'index' : true },

               { 'name' : 'remote_ip', 'datatype' : 'ipaddr', 'index' : false },
               { 'name' : 'client_port', 'datatype' : 'int', 'index' : false },
   
               { 'name' : 'vm', 'datatype' : 'string', 'index' : false },                             
               { 'name' : 'vrouter', 'datatype' : 'string', 'index' : false },
               { 'name' : 'vrouter_ip', 'datatype' : 'ipaddr', 'index' : false },
               { 'name' : 'other_vrouter_ip', 'datatype' : 'ipaddr', 'index' : false },
               { 'name' : 'underlay_proto', 'datatype' : 'int', 'index' : false },
 
               // Flow based fields
               { 'name' : 'forward_flow_uuid', 'datatype' : 'uuid_t', 'index' : false },
               { 'name' : 'forward_action', 'datatype' : 'string', 'index' : false },
               { 'name' : 'forward_sg_rule_uuid', 'datatype' : 'uuid', 'index' : false },
               { 'name' : 'forward_nw_ace_uuid', 'datatype' : 'uuid', 'index' : false },              
               { 'name' : 'forward_underlay_source_port', 'datatype' : 'int', 'index' : false },
               { 'name' : 'forward_drop_reason', 'datatype' : 'string', 'index' : false },

               { 'name' : 'reverse_flow_uuid', 'datatype' : 'uuid_t', 'index' : false },
               { 'name' : 'reverse_action', 'datatype' : 'string', 'index' : false },
               { 'name' : 'reverse_sg_rule_uuid', 'datatype' : 'uuid', 'index' : false },
               { 'name' : 'reverse_nw_ace_uuid', 'datatype' : 'uuid', 'index' : false },              
               { 'name' : 'reverse_underlay_source_port', 'datatype' : 'int', 'index' : false },
               { 'name' : 'reverse_drop_reason', 'datatype' : 'string', 'index' : false },

               // Time-series fields
               { 'name' : 'session_class_id', 'datatype' : 'int', 'index' : false },
               { 'name' : 'T', 'datatype' : 'int', 'index' : false },
               { 'name' : 'T=', 'datatype' : 'int', 'index' : false },

               { 'name' : 'sum(forward_sampled_packets)', 'datatype' : 'int', 'index' : false },
               { 'name' : 'sum(forward_sampled_bytes)', 'datatype' : 'int', 'index' : false },
               { 'name' : 'sum(forward_logged_packets)', 'datatype' : 'int', 'index' : false },
               { 'name' : 'sum(forward_logged_bytes)', 'datatype' : 'int', 'index' : false },

               { 'name' : 'sum(reverse_sampled_packets)', 'datatype' : 'int', 'index' : false },
               { 'name' : 'sum(reverse_sampled_bytes)', 'datatype' : 'int', 'index' : false },
               { 'name' : 'sum(reverse_logged_packets)', 'datatype' : 'int', 'index' : false },
               { 'name' : 'sum(reverse_logged_bytes)', 'datatype' : 'int', 'index' : false },
            
               { 'name' : 'sample_count', 'datatype' : 'int', 'index' : false },

               ]
         },
         'columnvalues' : [ ],
    }
```
'is_service_instance' indicates whether the session belongs to a service instance that is used in the service chaining feature. For application level traffic analysis users will generally not be interested in analysis of sessions that belong to a service instance and hence if 'is_service_instance' is not specified in the query, the value of 'is_service_instance' will be considered as false.

For sessions where 'session_type' is 'server', the columns 'local_ip' and 'remote_ip' map to the IP address of the server and client respectively.
For sessions where 'session_type' is 'client', the columns 'local_ip' and 'remote_ip' map to the IP address of the client and server respectively.

We will continue support the existing flow query APIs by mapping it to session APIs with few changes
Currently for flows, contrail-analytics-api exposes 2 APIs - FlowRecordTable and FlowSeriesTable. we will support both the APIs where FlowRecordTable will be mapped to SessionRecordTable and FlowSeriesTable will be SessionSeriesTable. The translation part will be taken care in QueryEngine. The new Flow APIs will be as below:
```
{
     'name' : 'FlowRecordTable',
     'schema' : {
         'type' : 'FLOW',
         'columns' :  [
           { 'name' : 'vrouter', 'datatype' : 'string', 'index' : true, 'session_index' : false },
           { 'name' : 'sourcevn', 'datatype' : 'string', 'index' : true },
           { 'name' : 'sourceip', 'datatype' : 'ipaddr', 'index' : true },
           { 'name' : 'destvn', 'datatype' : 'string', 'index' : true },
           { 'name' : 'destip', 'datatype' : 'ipaddr', 'index' : true },
           { 'name' : 'protocol', 'datatype' : 'int', 'index' : true },
           { 'name' : 'sport', 'datatype' : 'int', 'index' : true },
           { 'name' : 'dport', 'datatype' : 'int', 'index' : true },
           { 'name' : 'direction_ing', 'datatype' : 'int', 'index' : true },
           { 'name' : 'UuidKey', 'datatype' : 'uuid', 'index' : false },
           { 'name' : 'setup_time', 'datatype' : 'long', 'index' : false },
           { 'name' : 'teardown_time', 'datatype' : 'long', 'index' : false },
           { 'name' : 'agg-packets', 'datatype' : 'long', 'index' : false },
           { 'name' : 'agg-bytes', 'datatype' : 'long', 'index' : false },
           { 'name' : 'action', 'datatype' : 'string', 'index' : false },
           { 'name' : 'sg_rule_uuid', 'datatype' : 'uuid', 'index' : false },
           { 'name' : 'nw_ace_uuid', 'datatype' : 'uuid', 'index' : false },
           { 'name' : 'vrouter_ip', 'datatype' : 'string', 'index' : false },
           { 'name' : 'other_vrouter_ip', 'datatype' : 'string', 'index' : false },
           { 'name' : 'underlay_proto', 'datatype' : 'int', 'index' : false },
           { 'name' : 'underlay_source_port', 'datatype' : 'int', 'index' : false },
           { 'name' : 'vmi_uuid', 'datatype' : 'uuid', 'index' : false },
           { 'name' : 'drop_reason', 'datatype' : 'string', 'index' : false },
           ]
     },
     'columnvalues' : [ ],
},
{
     'name' : 'FlowSeriesTable',
     'schema' : {
         'type' : 'FLOW',
         'columns' :  [
           { 'name' : 'vrouter', 'datatype' : 'string', 'index' : true , 'session_index' : false},
           { 'name' : 'sourcevn', 'datatype' : 'string', 'index' : true },
           { 'name' : 'sourceip', 'datatype' : 'ipaddr', 'index' : true },
           { 'name' : 'destvn', 'datatype' : 'string', 'index' : true },
           { 'name' : 'destip', 'datatype' : 'ipaddr', 'index' : true },
           { 'name' : 'protocol', 'datatype' : 'int', 'index' : true },
           { 'name' : 'sport', 'datatype' : 'int', 'index' : true },
           { 'name' : 'dport', 'datatype' : 'int', 'index' : true },
           { 'name' : 'direction_ing', 'datatype' : 'int', 'index' : true },
           { 'name' : 'flow_class_id', 'datatype' : 'int', 'index' : false },
           { 'name' : 'T', 'datatype' : 'int', 'index' : false },
           { 'name' : 'T=', 'datatype' : 'int', 'index' : false },
           { 'name' : 'packets', 'datatype' : 'int', 'index' : false },
           { 'name' : 'bytes', 'datatype' : 'int', 'index' : false },
           { 'name' : 'SUM(packets)', 'datatype' : 'int', 'index' : false },
           { 'name' : 'SUM(bytes)', 'datatype' : 'int', 'index' : false },
           ]
     },
     'columnvalues' : [ ],
}
```
# 5. Performance and scaling impact
## 5.1 API and control plane


## 5.2 Forwarding performance


# 6. Upgrade


# 7. Deprecations


# 8. Dependencies


# 9. Testing
## 9.1 Unit tests
## 9.2 Dev tests
## 9.3 System tests

# 10. Documentation Impact

# 11. References

