
# 1. Introduction
This document goes over the 'security logging object' feature.

# 2. Problem statement
* Security events including traffic session ACCEPTs and DROPs due to
 enforcement of policy and security groups have to be logged to Analytics.
 Configuration control is required to selectively enable logging for sessions
 matching specified policy rules or security groups.
* Reduce the amount of data sent from vRouter Agent to Analytics.
* Even with reduced information flowing into analytics, the flows should be
 debuggable at all entry and exit points (no information loss)

# 3. Proposed solution
* Send Session record from agent to analytics combining the data for both the
 forward and reverse flows in a single message.
* Add configuration control to selectively enable logging for sessions matching
 specified policy rules or security groups.
* Modify analytics to consume the new session records and change the query
 engine to fetch data from the modified tables.
* Agent will be sending two kinds of information to analytics
  * FlowSample - Sampled session data for flows sampled by the algorithm
  * FlowLog - flows corresponding to SLO object.
* The destination for FlowSample messages is just analytics. The destinations
  supported for FlowLog are Analytics, syslog and local file. The
  destinations are mutually exclusive. Flags will be created to specify the
  different destinations. When same flow is SLO configured and also selected by
  sampling algorithm, the corrersponding session record can go to both
  analytics and syslog/local file.

## 3.1 Alternatives considered
#### Describe pros and cons of alternatives considered.

## 3.2 API schema changes
* A new Security-logging object (SLO) will be introduced.
* SLO can be created at global level or at tenant level
* Tenant quota for SLO
* SLO can be attached to:  
    i.   Network (log hits that have matching rule in SLO for all interfaces in
         this network)  
    ii.  Interface (log all hits that have matching rule in SLO, for this
         interface.)  
* Enabled globally at tenant level or global level depending where the object
 was created.
* Fields of SLO  
    i.   List of {(security-group, rule-uuid, rate)}   
    ii.  List of {(network-policy, rule-uuid, rate)}   

            (Rule-uuid can be *, which implies all rules from the policy or
             security group)   
            (Rate controls how many flows are logged. The first session in  
             every R (rate) number of sessions matching the SLO will be logged.  
             When the rate is set to 1, all sessions are logged.)  
    iii. Enable/disable this SLO  
    iv.  SLO rate, which is the logging rate if rate is not specified in the
         rule list  
* Knob to enable/disable security logging at global level.
* Analytics apis to query flows should now have an option to specify sampled
  session record or logged session record should be added.
* Flags to be added to specify the destinations for FlowLog messages.

## 3.3 User workflow impact

## 3.4 UI changes

## 3.5 Notification impact
The following structure would be populated and sent

```
Struct SessionLogData {
   1: FlowRecordData ingress;
   2: FlowRecordData egress;
    /* Below fields are common can be sent once */
   3: bool is_ingress_flow_forward;
   4: string ingress_sourcevn;
   5: string ingress_sourceip;
   6: string ingress_destvn;
   7: string ingress_destip;
   8: byte ingress_protocol;
   9: i16 ingress_sport;
   10: i16 ingress_dport;
   11: vrouter_ip;
   12: other_vrouter_ip;
  /* 0 – sampled, 1-logged, 2-sampled & logged */
   13: byte sampled_or_logged;
   14: bool local_flow;
}
```
```
struct FlowRecordData {
   1: string flowuuid;
   2: optional u16 tcp_flags;
   3: optional string vm;
   4: optional i64 setup_time;
   5: optional i64       teardown_time;
   6: optional i64       log_diff_bytes;
   7: optional i64       log_diff_packets;
   8: optional i64       sampled_diff_bytes;
   9: optional i64       sampled_diff_packets;
   10: optional string    action;
   11: optional string    sg_rule_uuid;
   12: optional string    nw_ace_uuid;
   13: optional u16       underlay_proto;
   14: optional u16       underlay_source_port;
   15: optional string    vmi_uuid;
   16: optional string    drop_reason;
}
```

# 4. Implementation
## 4.1 Assignee(s)
#### List dev and test assignments

## 4.2 Work items  
### 4.2.1 Agent Changes

Each session record will have three parts  

* Record key  
* Forward and Reverse Flow logging data  
* Forward and Reverse Flow sampling data  

The flow sampling algorithm continues as is with the change that session records
 are sent to Analytics when a flow is selected by the sampling algorithm. The
 flow sampling rate can be configured as is done currently (in R3.x releases)
 to control the export of flow data export from vRouter Agent to Analytics.

   Based on provisioned configuration, the flow log records may go to
 local files on the compute node or are sent to  analytics. Local log files
 created for this are managed by rotating regularly. Flags to be provided to
 control the destinations to which FlowRecords will be sent.

   Flags to be added in the agent config file to specify the destinations for
 FlowLog messages.

### 4.2.2 Analytics Changes  

In the analytics node, changes will be needed to handle the new session log
 data. Additionally the session log data will be indexed only against the source
 and destination index table to reduce the writes (currently each flow record is
 indexed against 5 tables).

  The schema for the Source and Destination index tables have to be
 changed, so that they can be queried against all possible cases and should not
 result in any information loss.The query process/ write process has to change
 to accommodate the new index tables and removal of the other index tables
viz., flowtableprotdpver2, flowtableprotspver2, flowtablevrouterver2.

  All the queries against the sessions which are logged should be
specified explicitly while querying (if left unspecified, query will be
performed against the sampled data). This will change the implementation of
where query processing which looks up the respective index tables against which
queries are issued. With new index table, processing of query involving
FlowSeriesTables have to change; necessary logic to be added to fetch values
 even if the FlowSeriesQueries are to be queried against the secondary or
 tertiary indices viz., port no, protocol,..

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
