
# 1. Introduction
This document goes over the 'security logging object' feature.

# 2. Problem statement
* Security events including traffic session ACCEPTs and DROPs due to
 enforcement of policy and security groups have to be logged to Analytics.
 Configuration control is required to selectively enable logging for sessions
 matching specified policy rules or security groups. Configuration control is
 also required to selectively enable session logging at global level.
* Reduce the amount of data sent from vRouter Agent to Analytics.
* Even with reduced information flowing into analytics, the flows should be
 debuggable at all entry and exit points (no information loss)

# 3. Proposed solution
* Send Session record from agent to analytics combining the data for both the
 forward and reverse flows in a single message.
* Associate the referred session as client session if it is ingress forward
  session or as server session if it is egress forward session.
* Pre aggregate the sessions based on the lesser important elements from the
  given 7 tuple.
* Add configuration control to selectively enable logging for sessions matching
 specified network policy rules or security groups.
* Incorporation of security framework tag-attributes : Deployment, App, Tier and
  site. VMI groups are categorized according to one or more values of these
  tags.
* SessionEndPoint message will be introduced instead of existing Flow message.
  This newer message will be used to analyze the traffic between VMI groups.
* Modify analytics to consume the new session records and change the query
 engine to fetch data from the modified tables.
* Agent will be sending single session data for both sampled and logged data
  * SessionSample - Sampled session data for sessions sampled by the algorithm
  * SessionLog - session corresponding to SLO object.
* The destination for SessionSample messages is just analytics. The destinations
  supported for SessionLog are Analytics, syslog and local file. The
  destinations are mutually exclusive. Flags will be created to specify the
  different destinations. When same session is SLO configured and also selected
  by sampling algorithm, the corresponding session record can go to both
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
            (Rate controls how many sessions are logged. The first session in  
             every R (rate) number of sessions matching the SLO will be logged.  
             When the rate is set to 1, all sessions are logged.)  
    iii. Enable/disable this SLO  
    iv.  SLO rate, which is the logging rate if rate is not specified in the
         rule list  
* Knob to enable/disable security logging at global level.
* Analytics apis to query flows should now have an option to specify sampled
  session record or logged session record should be added.
* Flags to be added to specify the destinations for SessionLog messages.
 
## 3.3 User workflow impact

## 3.4 UI changes
Following level of configuration will be required from UI
* Configuration for SLO object need to be added
* Association of Policy Rule or Security Groups to SLO need to be added
* Attachment of SLO to Global, VN and Interface level need to be added

## 3.5 Notification impact
The following structure would be populated and sent

```
struct SessionEndpoint {
    1: String VMI
    2: String src_vrouter
    3: String VN
    4: String Deployment
    5: String Tier
    6: String App
    7: String Site
    8: String Security_Policy_Rule
    9: String Remote_Deployment
    10: String Remote_Tier
    11: String Remote_App
    12: String Remote_Site
    13: String Remote_Prefix  // Only use when route has no Contrail tags
    14: String Remote_VN
    15: Bool IsClientSession
    16: Bool IsServiceInstance
    
    // For client <protocol, dport, SIP>. For server <protocol, dport, DIP>
    17: Map<u16, Map<u16, Map <ipaddr, SessionAggregateInfo> > >aggMap;
}
```
```
SessionAggregateInfo {
    1: i64 tx_bytes;
    2: i64 rx_bytes;
    3: i64 tx_pkts;
    4: i64 rx_pkts;
    // For client <DIP,sport>. For server <SIP,sport>
    5: Map<ipaddr, Map<u16, SessionData> > sessionMap
}
```
```
struct SessionData{
    1: i64 tx_bytes_sampled;
    2: i64 rx_bytes_sampled;
    3: i64 tx_pkts_sampled;
    4: i64 rx_pkts_sampled;
    5: i64 tx_bytes_logged;
    6: i64 rx_bytes_logged;
    7: i64 tx_pkts_logged;
    8: i64 rx_pkts_logged;
    9: bool IsSampled; 
    10: bool IsLogged;
}
```

# 4. Implementation
## 4.1 Assignee(s)
#### List dev and test assignments

## 4.2 Work items  
### 4.2.1 Agent Changes
#### Association of client/server sessions
* If the agent sees the session packet in the direction of ingress (towards the
  fabric), it creates a flow in both directions and considers the
  flow as ingress forward. This ingress forward session will be referred as
  client session.
* If the agent sees the session packet in the direction of egress (away from the
  fabric), it creates a flow in both directions and considers the
  flow as egress forward. This egress forward session will be referred as
  server session.

#### Session End Point Message Components
* The identity of the SessionEndpoint, which is a combination of
    i.   VMI. It includes its tags and VNs.
    ii.  Security Policy and Security Rule.
    iii. Route attributes for the remote endpoint – tags and VN. For North-South
         routes and ECMP routes, use the route prefix as well.
* A SessionAggregate map
    i.   Key for this map
            Client Session : Source IP + Protocol + Destination Port
            Server Session : Destination IP + Protocol + Destination Port
    ii.  Session Map
            Key for this map
                Client Session : Destination IP + Source Port
                Server Session : Source IP + Destination Port
            Session specific count
                Session specific Rx/Tx bytes/packets for both sample and log.
            Flag to indicate the sample and log
    iii. SessionAggregate counts
            Aggregated statistics information about all the sessions that are
            mapped to this aggregate map. This is reflected in the
            Rx/Tx bytes/packets.

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
Following test cases need to be verified
* Config flow test cases
    i.   SLO config creation without any network policy and security group.
    ii.  SLO config creation with all rules from the given network policy.
    iii. SLO config creation with specific rules from the given network policy.
    iv.  SLO config creation with all rules from the given security group.
    v.   SLO config creation with specific rules from the given security group.
    vi.  SLO attachment at the global level.
    vii. SLO attachment at the VMI level.
    viii.SLO attachment at the VN level.
* Construction of session end point message test cases
    i.   Session detection for the given flow (client/server).
    ii.  Creation of session aggregate map for the given session flow.
    iii. Association of session maps to session aggregate map.
* Aggregation for multiple sessions
    i.   Aggregation of multiple sessions based on the client session.
    ii.  Aggregation of multiple sessions based on the server session.
## 9.2 Dev tests
* Complete config flow from UI to agent for SLO objects
    i.   SLO config creation at different attachment levels.
    ii.  SLO config change at different attachment levels.
* Verification of aggregated flow in collector
    i.   Decoding session end point messages from client.
    ii.  Decoding session end point messages from server.
## 9.3 System tests
* Complete functionality testing for SLO.
* Performance testing
    i.   testing of performance impact with different level of destinations for
         logging : syslog, local file and collector
    ii.  with higher scale of flows, testing of performance impact with respect
         to vrouter agent
    iii. with higher scale of flows, testing of performance improvement due to
         the aggregation of sessions.

# 10. Documentation Impact

# 11. References
