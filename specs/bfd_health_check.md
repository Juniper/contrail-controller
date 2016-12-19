#1. Introduction
Provide Bidirectional Forwarding and Detection (BFD) protocol based health check
over Virtual Machine Interfaces (VMIs)

#2. Problem statement
Currently, one can enable health-check based on ping and curl commands. When
enabled, these are run periodically, once every few seconds. Hence failure
detection times using these can be quite large, always in the order of seconds

#3. Proposed solution
Use BFD to detect failure and recovery instead of ping/curl. BFD runs quite
aggressively and typically can detect end point failures in the order of milli
seconds. When used in an event (notify) driven model (instead of poll), one
can achieve failure detection and recovery in sub-second intervals

##3.1 Alternatives considered
Health-Check for VMIs is already supported based in ping and curl commands.
However, they are poll based and do not provide sub-second based resolution.

Also, ping/curl does not necessarily mean that applications are indeed alive.
In typical BFD implementations, applications are notified immediately upon
BFD session state changes

##3.2 API schema changes
BFD can be an additional enumeration to current list of health-checkers.

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

```

##3.3 User workflow impact
####Describe how users will use the feature.

##3.4 UI changes
UI shall provide a way to configure BFD as a way to do VMI Health Check

##3.5 Notification impact
####Describe any log, UVE, alarm changes

#4. Implementation
Currently BFD Server/Client code already exists in src/bfd as a stand-alone
module (or process). Also, this has a REST based interface to add, delete and
monitor BFD sessions.

Agent has health-check infrastructure, where in ping/curl based checks are made
periodically using poll model (when ping/curl command completes, they are
re-initiated)

With BFD, we can use a event based approach on top of existing health-check
infra. health-check infra can provide a REST interface to accept BFD state
change notifications.

BFD server code should accept this REST URL as part of monitor REST request.
When ever BFD state changes, BFD module can use this REST URL of health-checker
piece of the agent to inform the same. health-checker can then treat the state
change similar to how it does when it receives ping/curl result when those
commands complete and exit

#5. Performance and scaling impact
5.1 CPU usage can increase quite significantly if it is enabled over all VMIs
in a compute node. Since both end points of the BFD reside in the same physical
system, shared resource 'CPU' can become potentially a bottle neck. Hence BFD
must be enabled judiciously on selective VMIs

##5.2 Forwarding performance
Forwarding performance can get affected as CPUs are shared for both ends of the
BFD session as well as for all other data traffic forwarding

#6. Upgrade
####Describe upgrade impact of the feature
####Schema migration/transition

#7. Deprecations
####If this feature deprecates any older feature or API then list it here.

#8. Dependencies
####Describe dependent features or components.

#9. Testing
##9.1 Unit tests
##9.2 Dev tests
##9.3 System tests

#10. Documentation Impact

#11. References
* BFD RFC [RFC5880](https://tools.ietf.org/html/rfc5880)
* Seemless BFD [RFC7880](https://tools.ietf.org/html/rfc7880)
* BFD [source](https://github.com/Juniper/contrail-controller/tree/master/src/bfd)
* Health-Checker in [Agent](https://github.com/Juniper/contrail-controller/blob/master/src/vnsw/agent/oper/health_check.cc)
