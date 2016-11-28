
#1. Introduction
Integrate Contrail BMS (Bare Metal Service) with OpenStack Ironic service.

#2. Problem statement
Presently, there are two independent solutions. One in OpenStack by using Ironic service and second in Contrail using Contrail BMS service. These two solutions work independently. It is a desire by some customers to provide an integrated solution.

#3. Proposed solution
In order to integrate OpenStack Ironic with Contrail BMS, following enhancements are required:
* Support enhanced Neutron Port Create/Update API to accept port binding information
* Implement support for LAG/MLAG to support port-groups
* Implement support for Security Groups to configure ACLs on the QFS switch(es)
* The physical connectivity information of the bare metal servers should be extracted from the binding profile of the Neutron Port Create API instead of from the configuration server.
The details are described in [Ironic-Contrail BMS Integration Proposal](https://docs.google.com/document/d/1qAV_qLIJM9PNcSm_h_ag1J91zOXegSZYjcEA6BjdWhk/edit#heading=h.whds97hzqqe4a)

##3.1 Alternatives considered
None.
Keep both solutions independent and let customers pick the one which works best for them.

##3.2 API schema changes
Details TBD

##3.3 User workflow impact
In order to offer baremetal service to the end-users, the operators are required to perform following steps:

1. Create Ironic Nodes (ironic node-create).
2. Create ironic ports associated with the nodes (ironic port-create)  - They must specify “local_link_connection” information. This contains three items;
   * Switch-id - this is the MAC address of the TOR switch where a given port of the BM host is connected
   * Port-id - this is the actual physical port on TOR where baremetal server is connected (e.g. Ethernet9, Eth20)
   * Switch-info - this is vendor specific information - Switch IP may be specified here. In case of a controller (e.g. Contrail), an identifier/handle by which controller can identify the appropriate TOR may be specified here.
3. Create ironic port groups - if multiple ports from NIC(s) of BM host connect to multiple ports on the TOR switches. Port Group contains a list of “local_link_connection” dict. For instance if a server connects to two TORs (MLAG configuration), the list will contain one entry for each physical port’s local_link_connection.
4. Create baremetal flavors (nova flavor-create) - these flavors are visible to the end-user. This is used for the scheduling and node selection purposes
5. Create appropriate images to be deployed on the baremetal server (glance image-create)
6. Create a Provisioning Network - (neutron net-create and neutron subnet-create).
7. Create a Cleaning Network - similar to provisioning network
8. Create Security Groups for Provisioning (and or Cleaning) network(s) - (neutron security-group-create and neutron security-group-rule-create)


##3.4 UI changes
No change to the UI.

##3.5 Notification impact
TBD

#4. Implementation
##4.1 Work items
Details to be figured out once this program is given a Go.

#5. Performance and scaling impact
##5.1 API and control plane
No Impact

##5.2 Forwarding performance
No Impact

#6. Upgrade
TBD

#7. Deprecations
None

#8. Dependencies
TBD

#9. Testing
##9.1 Unit tests
##9.2 Dev tests
##9.3 System tests

#10. Documentation Impact
Following documents will be updated:
1. OpenStack Ironic Wiki will be updated to reflect the support for Contrail
2. Contrail BMS service documentation will be updated to describe Ironic integration

#11. References
* [Ironic-Contrail BMS Integration Proposal](https://docs.google.com/document/d/1qAV_qLIJM9PNcSm_h_ag1J91zOXegSZYjcEA6BjdWhk/edit#heading=h.whds97hzqqe4a)
