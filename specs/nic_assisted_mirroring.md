
#1. Introduction

#2. Problem statement
When mirroring is enabled, the vrouter throughput reduces because of the additional packet handling overheads.
A solution to avoid impact on throughput due to mirroring is to use NIC’s mirroring capabilities.


#3. Proposed solution
In this approach, the vrouter doesn’t mirror the packets.  When NIC assisted mirroring is enabled, ingress packets coming from the VM that have to be mirrored are sent to the NIC with a configured vlan tag. The NIC is programmed to do VLAN port mirroring, that is any packet with the configured VLAN is mirrored additionally by the NIC. Note that this change in vrouter is only for traffic coming from the VMs. Traffic coming from the fabric is directly mirrored from the NIC itself and there is no additional mirroring need in vrouter.

The programming of the NIC itself for appropriate mirroring is outside the scope of the current activity.

##3.1 Alternatives considered

##3.2 API schema changes
The configuration impact is to support additional options in the mirroring action to configure NIC assisted mirroring and vlan tag to use in such a case.

##3.3 User workflow impact

##3.4 UI changes

##3.5 Notification impact


#4. Implementation
##4.1 Assignee(s)

##4.2 Work items

#5. Performance and scaling impact
##5.1 API and control plane

##5.2 Forwarding performance

#6. Upgrade

#7. Deprecations

#8. Dependencies

#9. Testing
##9.1 Unit tests
##9.2 Dev tests
##9.3 System tests

#10. Documentation Impact

#11. References
