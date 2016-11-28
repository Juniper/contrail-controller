
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

A detailed integration proposal, along with some basic Ironc background is described in the following sub-sections:

## Overview

Ironic is an OpenStack project which orchestrates baremetal deployments. It utilizes Nova API  (to create baremetal flavors and launch baremetal servers), Glance service for image management, and Neutron to support multi-tenancy.

Contrail BMS service offers similar baremetal deployments, but, does not utilize Ironic lifecycle management of the baremetal servers.

This proposal highlights the integration points to facilitate the integration of Ironic and Contrail BMS services.

## Ironic High Level Details

The purpose of this section is to give readers some basic idea of inner workings of Ironic that is  relevant to this integration proposal. This is also for the benefit of unfamiliar readers. There is a huge amount of documentation available. To make it easy for the readers, links to appropriate and relevant documentation are provided in this writeup.

Ironic consists of two main components; Conductor (ir-cond) and API (ir-api).

### Ironic API

Ironic API is not meant for end-users. They use Nova API for baremetal instances in the same manner as they use it for virtual instances. Ironic API is for operators (or admins). This API is utilized to create Ironic Nodes, Ports, Port Groups, and to specify drivers. See [Ironic API](http://developer.openstack.org/api-ref/baremetal/) for details. In a typical deployment scenario an operator creates nodes and ports (and/ or port groups). A node represents a baremetal machine and a port represents a NIC of a baremetal machine. Port Groups represent LAG and MLAG interfaces.

### Ironic Conductor

Ironic Conductor manages the lifecycle of baremetal instances. See the lifecycle [state machine](http://docs.openstack.org/developer/ironic/dev/states.html) of Ironic conductor. There is a proposal to enhance this state machine, but, that is not really relevant for this discussion, hence, is not discussed here.

Ironic driver, a sub-component of Ironic conductor plugs into Nova virt driver framework. When a baremetal instance is launched (based upon the flavor type), instead of virt driver, ironic driver takes over and manages the booting of the instance and also manages the Nova-Ironic interface. Most of Ironic-Neutron integration goes into this driver. (see below for details)

## Ironic-Neutron Multi-tenancy details

In order to support multi-tenancy and to avoid spoofing/hacking during bootup or cleanup of the baremetal instances, three networks are utilized.

1. **Tenant Network** - this is the desired network where the end-user wants its baremetal server to connect when it is fully operational.

2. **Provisioning Network** - this is an internal network visible only to the operators/admis and is utilized during the provisioning phase of the baremetal instance. Once the instance is fully booted up and operational, a network flip takes place (orchestrated by ionic-conductor) which moves the instance from provisioning network to tenant network

3. **Cleaning Network** - this is also internal network. When an end-user shuts down the baremetal instance, ironic-conductor performs a network flip and takes the instance off from tenant network and moves to the cleaning network. All cleaning activities takes place on this network

Depending upon the network where server is connected, its DHCP server is utilized to get the IP address for the server. Therefore, a server utilizes three different IP addresses during its lifecycle. However, only one IP address is visible to the tenant.

Similarly, up to three sets of security groups may be utilized during the life cycle of the server - depending upon which network the server is connected to a given time.

Note: Ironic utilizes Neutron’s DHCP server as well Security Groups.

### Deployment Steps by Admins/Operators

In order to offer baremetal service to the end-users, the operators are required to perform following steps:

1. Create Ironic Nodes (ironic node-create).

2. Create ironic ports associated with the nodes (ironic port-create)  - They must specify "local_link_connection" information. This contains three items;

    1. Switch-id - this is the MAC address of the TOR switch where a given port is connected

    2. Port-id - this is the actual physical port on TOR where baremetal server is connected (e.g. Ethernet9, Eth20)

    3. Switch-info - this is vendor specific information - Switch IP may be specified here or in case of a controller (e.g. Contrail), an identifier/handle by which controller can identify the appropriate TOR

3. Create ironic port groups - if multiple ports from the NICs connect to multiple ports on the TOR switches. Port Group contains a list of "local_link_connection" dict. For instance if a server connects to two TORs (MLAG configuration), the list will contain one entry for each physical port’s local_link_connection.

4. Create baremetal flavors (nova flavor-create) - these flavors are visible to the end-user. This is used for the scheduling and node selection purposes

5. Create appropriate images to be deployed on the baremetal server (glance image-create)

6. Create a Provisioning Network - (neutron net-create and neutron subnet-create).

7. Create a Cleaning Network - similar to provisioning network

8. Create Security Groups for Provisioning (and or Cleaning) network(s) - (neutron security-group-create and neutron security-group-rule-create)

### Detailed Workflow

Following figure describes the workflow that takes place to instantiate a baremetal server. Only important details that are relevant to this discussion are shown in the figure. Additionally, in the neutron port create/delete/update requests only critical parts of the port dict are shown.

![image alt text](image_0.png)

### Neutron Plugin details

In above figure, the flow between Neutron and BM Server is shown in a very abbreviated manner. Those details are critical and warrant detailed explanation. These details are described below:

Ironic-Neutron integration was designed with ML2 Core plugin (and ML2 drivers) in mind and relies on the core plugin’s port-binding implementation. The core plugin implicitly invokes bind_port() methods on registered ML2 drivers in response to any port create and update request. bind_port() was originally designed to facilitate the handshake between OVS (and linux bridge) drivers with Nova to ensure that the network plumbing takes place in a coordinated fashion during the bootup sequence of the instance. Later it was enhanced to support Hierarchical Port Binding (HPB) - later we discuss why this is relevant to this discussion.

Since baremetal provisioning does not involve OVS (or linux bridge), this integration have been significantly simplified and is left on the vendors (their ML2 drivers or plugins) to ensure networking is plumbed at the appropriate stage of server bootup. Ironic conductor plays a critical role to facilitate the network flip - hence, the need of handshake has been significantly simplified/reduced.

In above figure, pay attention to the first create_port() request which is sent from Nova to Neutron. This requests has "host-id" set to None - this is not by accident, but, by design. Port binding framework works with the host-id. If host-id is not present, it does not know where/how to bind the port - hence, the configuration of the TOR is skipped. This is necessary step. Also note in the same request the vnic_type is “normal” and there is no “local_link_connection” information. The reason for this is that Nova does not have any information about the baremetal services. Nova picks the node (which matches the requested flavor - in this case it is a baremetal node) and invokes networking API during its allocate_for_instance() processing - which is common for all instances (virtual as well as baremetal). When virt driver is invoked, based upon the flavor type, ironic driver’s derived class is invoked and Ironic conductor takes over the operation, instead of virt driver.

Ironic conductor issues a new create_port request on the provisioning network to neutron. This request has all required information in the port dict, hence, the ML2 core plugin will bind the port and invoke the registered drivers. Rest of the flow is self explanatory - except for the last step. Note that the last step is an update_port() request instead of create_port(). Because Nova has already created that port on the "tenant-net" in the very first step. The same port is now updated. Since update port carries all the correct information, binding takes place, and hence, the servers is connected to the the correct tenant network.

**NOTE**: While the design was implemented with ML2 Core plugin in mind, there is nothing in the design which prevents the monolithic plugins to take full advantage of this framework. OVN plugin does exactly that. IBM (SoftLayer) is building their cloud based upon OVN controller and it uses this Ironic-Neutron integration to implement the baremetal deployment.

This proposal presents the similar model for Contrail integration.

## Contrail BMS Integration Points

There are two parts to this proposed integration:

### Physical Connectivity Information

As described above, the physical connectivity information is now presented in the create/update port request. Therefore, the contrail configuration steps requiring this information should be by-passed when Ironic is used north of contrail. Instead, this information should be taken from the port create/update request.

The details of local_link_connection are:

local_link_connection = {

		‘switch_id’: <switch-id>  - this is presented in MAC address notation and

     represents the mac address of the TOR switch

                        ‘port_id’: <port-id>         - the physical port on TOR where BM is connected

		‘switch_info’: "switch-info" - this is a string - vendors can put anything here

}

In above dict, switch_info can carry IP address of the switch, hostname, or anything vendor specific

### Neutron API Modifications

Following enhancements to the Contrail API server module are required:

In create/update port API, check for vnic_type. If ‘baremetal’, then look for host_id.

1. If host_id not set, save the network_id and port_id - as this port ID will be updated later. Skip the rest of the processing for this API

2. If host_id is set, check for the ‘local_link_connection’ dict and perform the network provisioning on the port/switch specified

### Port Groups (LAG/MLAG) Support

Initial version of this implementation will not support this feature. This will be added down the road.
In order to support this, following considerations are needed:

1. API modifications are similar as described above, except, the provisioning may span more than one TOR.

2. LAG or MLAG feature support in the TOR switch.

3. If ports on the TOR are not pre-configured in LAG/MLAG mode, should the API reject the request or proactively configure LAG/MLAG mode before pushing the networking configuration. Or should this step be pushed to Operators to pre-configure TORs as part of admin steps for deployment along with other steps described [here](#heading=h.7tb7nbf3ixkg). Does this issue apply to QFX product line?

4. Do we need to worry about the MAC address of the port group vs the MAC address of individual ports in the bundle? What if there is a conflict? Does this issue apply to QFX product line?

5. If an individual port is added to the port group - will the MLAG config be pushed to it automatically - what if there are any conflicts?

Note: all of these issues are related to the product feature support - may or may not be an issue. They are listed here just for the completeness sake.

### Security Groups (ACL) Support

Ironic uses Neutron security groups. As described in [Deployment Steps by Admins/Operators](#heading=h.7tb7nbf3ixkg), these security groups may be combination of admin defined as well as tenant defined.

When a port create/update request is sent to neutron, it contains a list of security group IDs. This is either filled in by Ironic (for provisioning and cleaning networks) or Nova (for tenant networks). This list is not shown in the above figure (to prevent the cluttering of the figure), but, this exists. This support will be available in Ocata release as well. If no security groups are specified either by Nova or Neutron, default security groups are used.

Note: Neutron does not normalize/merge multiple security rules, neither does it flag conflicting rules. Blatant conflicts/contradictions are caught and prevented. But conflicts/merges due to indirect security group rules are left to the vendors to deal with.

### Hierarchical Port Binding (HPB) Consideration

Ironic assumes (and supports) VLAN based networking.

Most of baremetal deployments require VLANs between TOR and Baremetal Servers and VxLAN (or other overlays and/or L3 networking) north of TOR switches - i.e. multi-segmented networks.

All of the network_ids mentioned in this section assume VALNs.

## Ironic Inspector

Ironic Inspector is mentioned here for informational purposes only and does not impact this integration. The reason of mentioning this is that this makes Ironic based deployments very compelling. Ironic Inspector automates the discovery of physical connectivity.

Most painful and error prone task about baremetal deployments is that operators have to manually supply  the physical connectivity information of the baremetal machines (one described by local_link_connection). Ironic Inspector uses LLDP based approach to discover the connected TORs, their IDs and Port information and automatically populates the Ironic DB, which is then used to plumb the networks. This makes move and relocations of the physical machines painless - hence, makes Ironic based deployments very desirable.

### Future work

Following features are in the works in Ironic and will be released within next couple of releases (including Ocata release).

1. **Port Groups Support** - already described in this writeup. Coming in Ocata release

2. **Security Groups Support** - already described in this writeup. Support for tenant networks already exists, but, for provisioning and cleaning networks coming in Ocata release.

3. **VLAN aware Servers** - Also known as Trunk port support. When a baremetal server is used to host services (e.g. FW, LB), one physical port needs to connect to multiple networks, i.e. multiple VLANs on a single port - hence, the name VLAN aware instances. This feature was implemented for VMs in Neutron release. Now it is being worked on for baremetal servers. This is targeted for Pike release, but, there is a desire to bring it in Ocata - this is being pushed by Cisco

4. **NIC to Network Mapping** - This is a Nova feature to support baremetal deployment. In the present implementation of end-users can specify port or network when they launch an instance and want to connect to multiple networks (e.g. nova boot --nic net=<net-id> --nic net=<net2-id>). This is sufficient for VMs. For baremetal instances, they tend to have multiple NICs with multiple ports. The desire is to be able to specify that a specific NIC port connects to a specific network (e.g. nova boot --nic_id==<nic-id> net=<net-id>)

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
References to the relevant Ironic documentation are provided in the backgroud section.
