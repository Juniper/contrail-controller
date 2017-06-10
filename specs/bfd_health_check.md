# 1. Introduction
Provide Bidirectional Forwarding and Detection (BFD) protocol based health check
over Virtual Machine Interfaces (VMIs).

# 2. Problem statement
Currently, one can enable health-check based on ping and curl commands. When
enabled, these are run periodically, once every few seconds. Hence failure
detection times using these can be quite large, always in the order of seconds.

# 3. Proposed solution
Use BFD to detect failure and recovery instead of ping/curl. BFD runs quite
aggressively and typically can detect end point failures in the order of milli
seconds. When used in an event (notify) driven model (instead of poll), one
can achieve failure detection and recovery in sub-second intervals.

## 3.1 Alternatives considered
Health-Check for VMIs is already supported based in ping and curl commands.
However, they are poll based and do not provide sub-second based resolution.

Also, ping/curl does not necessarily mean that applications are indeed alive.
In typical BFD implementations, applications are notified immediately upon BFD
session state changes.

## 3.2 API schema changes
BFD can be an additional enumeration to current list of health-checkers. Also
BFD parameters (in micro seconds) can be configured.

```
--- a/src/schema/vnc_cfg.xsd
+++ b/src/schema/vnc_cfg.xsd
@@ -2482,6 +2482,7 @@ targetNamespace="http://www.contrailsystems.com/2012/VNC-CONFIG/0"
     <xsd:restriction base="xsd:string">
         <xsd:enumeration value="PING"/>
         <xsd:enumeration value="HTTP"/>
+        <xsd:enumeration value="BFD"/>
     </xsd:restriction>
 </xsd:simpleType>

@@ -2537,11 +2538,15 @@ targetNamespace="http://www.contrailsystems.com/2012/VNC-CONFIG/0">
         <xsd:element name='health-check-type' type='HealthCheckType' required='true' operations='CRUD'
              description='Health check type, currently only link-local and end-to-end are supported'/>
         <xsd:element name='monitor-type' type='HealthCheckProtocolType' required='true' operations='CRUD'
-             description='Protocol used to monitor health, currently only HTTP and ICMP(ping) is supported'/>
+             description='Protocol used to monitor health, currently only HTTP, ICMP(ping), and BFD are supported'/>
         <xsd:element name='delay' type='xsd:integer' required='true' operations='CRUD'
-             description='Time in seconds  at which health check is repeated'/>
+             description='Time in seconds at which health check is repeated'/>
+        <xsd:element name='delayUsecs' type='xsd:integer' required='optional' operations='CRUD' default='0'
+             description='Time in micro seconds at which health check is repeated'/>
         <xsd:element name='timeout' type='xsd:integer' required='true' operations='CRUD'
              description='Time in seconds to wait for response'/>
+        <xsd:element name='timeoutUsecs' type='xsd:integer' required='optional' operations='CRUD' default='0'
+             description='Time in micro seconds to wait for response'/>
         <xsd:element name='max-retries' type='xsd:integer' required='true' operations='CRUD'
              description='Number of failures before declaring health bad'/>
         <xsd:element name='http-method' type='xsd:string' required='optional' operations='CRUD'

```

BFD configuration values can be computed as

if type == ServiceHealthCheckType.health_check_type = "BFD":
    desiredMinTxInterval    = ServiceHealthCheckType.delay +
                                  0.000001*ServiceHealthCheckType.delayUsecs
    requiredMinRxInterval   = ServiceHealthCheckType.timeout +
                                  0.000001*ServiceHealthCheckType.timeoutUsecs
    detectionTimeMultiplier = ServiceHealthCheckType.max_retries
else:
    delay                   = ServiceHealthCheckType.delay +
                                  0.000001*ServiceHealthCheckType.delayUsecs
    timeout                 = ServiceHealthCheckType.timeout +
                                  0.000001*ServiceHealthCheckType.timeoutUsecs
    max_retries             = ServiceHealthCheckType.max_retries

As noted in the "else" part above, it is the sum of seconds and microseconds
configured that takes into effect for all health-check types such as ICMP, HTTP
and BFD.

## 3.3 User workflow impact
####Describe how users will use the feature.

## 3.4 UI changes
UI shall provide a way to configure BFD as a way to do VMI Health Check

## 3.5 Notification impact
####Describe any log, UVE, alarm changes

# 4. Implementation
BFD Protocol shall be implemented in a library and this library is linked by
contrail-vrouter-agent binary. Existing agent-health-checker module shall make
direct api calls to bfd library to [de-]establish BFD sessions as necessary
based on the configuration.

BFD Library shall provide following APIs (and more) to agent-health-checker
module. This is just for reference and shall be changed as needed during actual
implementation.

```
namespace BFD {
ResultCode Client::RegisterClient(const string &client_name, ClientId *client_id);
ResultCode Client::UnregisterClient(const ClientId &client_id);
ResultCode Client::AddConnection(const boost::asio::ip::address& remote_host,
                                 const SessionConfig &config,
                                 const StateChangeNotificationCb callback);
ResultCode Client::DeleteConnection(const boost::asio::ip::address &remote_host);
}
```

Upon BFD State changes, typically (up/down), BFD module shall invoke agent
callback provided in the AddConnection() API. Health-Check module can take
necessary actions such as bringing VMI down (and thereby retracting routes
sourced from that interface) if BFD notifies that interface is down. This part
is similar to how health-checker handles response from ping/http based
health-check mechanism.

BFD Packets shall be sent and received over the pkt0 interface. This is the
internal communication interface between agent and vrouter forwarding module.
Since host kernel is not involved in the data plane, BFD packets can be received
from or sent to BFD destinations without the need for any NAT underneath.

# 5. Performance and scaling impact
5.1 CPU usage can increase quite significantly if it is enabled over all VMIs
in a compute node. Since both end points of the BFD reside in the same physical
system, shared resource 'CPU' can become potentially a bottle neck. Hence BFD
liveliness check must be enabled judiciously on selective VMIs.

##5.2 Forwarding performance
Forwarding performance can get affected as CPUs are shared for both ends of the
BFD session as well as for all other data traffic forwarding.

# 6. Upgrade
Since BFD is added a module with in the agent, most of the agent restart and
upgrade procedures remain the same. During controlled agent graceful-restart,
BFD can potentially negotiate longer time intervals and hence retain BFD.
sessions (This is further TBD...)

There is no specific issue with regards to Schema migration/transition due to this feature.

# 7. Deprecations
####If this feature deprecates any older feature or API then list it here.

# 8. Dependencies
####Describe dependent features or components.

# 9. Testing
## 9.1 Unit tests
Some unit tests are already present for BFD Server and BFD Protocol modules.
More shall be added in this area. Also new tests shall be added to thoroughly
test the BFD Client module and the interaction between BFD Client library and
agent health-checker module.

## 9.2 Dev tests
## 9.3 System tests

# 10. Documentation Impact

# 11. References
* BFD RFC [RFC5880](https://tools.ietf.org/html/rfc5880)
* Seamless BFD [RFC7880](https://tools.ietf.org/html/rfc7880)
* BFD [source](https://github.com/Juniper/contrail-controller/tree/master/src/bfd)
* Health-Check in [Agent](https://github.com/Juniper/contrail-controller/blob/master/src/vnsw/agent/oper/health_check.cc)
* Feature [BluePrint](https://blueprints.launchpad.net/juniperopenstack/+spec/bfd-over-vmis)
