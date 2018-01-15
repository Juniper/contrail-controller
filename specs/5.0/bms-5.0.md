# 1. Introduction
Contrail BMS (Bare Metal Server) Manager manages the life cycle management of BMS, which includes booting tenant desired image, and attaches them to the tenant network in a Multi-tenant clouds. The goal is to provide single click solution for the tenants in a similar manner as they are used to managing the virtual machines in their cloud environments.

# 2. Problem statement
Contrail Fabric product aims at providing seamless connectivity between virtual machines spawned in a multi-tenant cloud environment and bare metal servers residing on the same tenent network. The solution ensures end-to-end programming of networking equipment within the IP fabric, virtual machines and BMSs to ensure seamless, desired connectivity between the end points. The BMS manager is required to manage the BMSs in the environment, along with VMs which are managed by multi-tenant clouds and networking equipment, which is managed by device manager module of the contrail fabric.

# 3. Proposed solution
## 3.1 Assumptions

1. BMS manager makes use of several openstack and modules to on-board the target BMSs. Hence availability of cluster running these needed components (also called under-lay network) is assumed.
2. The operating system images to be used to bring up target servers are built by administrator separately (outside the scope of BMS manager) using utility such as disk image create (provided by openstack).
3. The parameters for target servers such as MAC address, IPMI credentials, hardware information needs to be provided by the administrator. i.e. there is no discovery of target servers.
4. The provisioning/deployment of target servers will use ironic.
5. Administrator will take care of setting up routing between openstack, contrail nodes, MX, QFXs so that reachability between these different devices is available.
6. MX needs to be configured to allow traffic between ironic provisioning network and openstack node.
7. In phase 1, BMS manager will implement deployment of target servers as available in ironic. Additional provisioning of servers, such as creating static routes, creating users etc. may be considered for future release. 

## 3.2 High level work flow

BMS manager will make use of different modules such as ironic, glance etc from a fully provisioned contrail cluster with openstack. 

Hence as a first step, a contrail cluster with openstack will need to be deployed. Deployment of the contrail cluster is outside the scope of BMS manager.
Using user interface, administrator will provide parameters for the server or node to be added to the BMS available nodes. BMS will generate a new node and add it to available nodes database.
Administator can use BMS image APIs to add images to glance. The creation of image is outside the scope of BMS. Standard utility such as create build image can be used for this purpose by administrator.
Administrator will create flavors for the BMS systems using BMS flavor management APIs.
Once administrator has created the images, nodes and flavors, he will be able to deploy any of the available nodes with any of the added images and flavors.
BMS will monitor the state of deployed servers and provide this information to analytics using Sandesh.
All the nodes available with BMS manager will be in available or deployed state. Administrator can choose the un-provision a deployed server and move it to available nodes. It is also possible to delete an available node from the list of nodes managed by BMS manager. A deplyed server must be unprovisioned and made available before it can be deleted from BMS node list.

Implementation details of BMS manager is described in the next section.

# 4. Implementation

# 5. Performance and scaling impact

## 5.1 API and control plane
None

## 5.2 Forwarding performance

# 6. Upgrade

# 7. Deprecations
None

# 8. Dependencies

# 9. Debugging

# 10. Testing
## 10.1 Unit tests
## 10.2 Dev tests
## 10.3 System tests

# 11. Documentation Impact
