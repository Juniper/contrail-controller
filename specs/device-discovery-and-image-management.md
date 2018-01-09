
# 1. Introduction
Contrail Fabric management use cases require management and automation of the underlay networks in the data center. This spec covers the design and implementation of extending existing Contrail config node to provide EMS capabilities on physical network elements, such as TOR/EOR switches, Spines, SDN gateway, and VPN gateways in the data center. User should be able to perform basic device management functions such device discovery, inventory import, image management, device image upgrade, etc. 

# 2. Problem statement
Contrail currently does not manage the underlay network in the data center and it assumes the underlay network is provisioned prior to the overlay network provisioning via Contrail. There is no automation of the underlay network provisioning and management. Because the underlay network is managed outside Contrail, admin needs to manually populate the Contrail VNC data model with underlay network info via UI or API. This process could be automated when the underlay network is managed by Contrail.

Many underlay network is built with devices from multiple vendors. One of the challenge to underlay network management is multi-vendor support. Each vendor's device has its own CLI commands, configuration schema, or APIs to configure and operate. 

#### User Stories
Here are the epic user stories that captures the requirements from PLM for underlay network onboarding and automation.

- [GLAC-2: Importing/Discovering DC Fabric and Service Objects](https://aspg-jira.juniper.net/browse/GLAC-2)
- [GLAC-7: DC Fabric and Service objects software upgrade](https://aspg-jira.juniper.net/browse/GLAC-7)
- [GLAC-47: Device Onboarding - brown field](https://aspg-jira.juniper.net/browse/GLAC-47)
- [GLAC-136: Device Topology Discovery](https://aspg-jira.juniper.net/browse/GLAC-136)

# 3. Proposed solution
The proposed solution is to extend Contrail config node with EMS functionality via Ansible. Diagram below shows the high level design of the propose solution.

![High Level Design](images/fabmgt_ems.png)

#### Why Ansible?
- Multi vendor support with rich plugin
- Extensible configuration management and automation with rich set of plugins 
- Easy to customize by network engineer during deployment

#### Why job?
- Some EMS functions such as image upgrade or RMA could take long time to execute and user needs feedback on the progress
- Many EMS functions are applied to multiple devices in a workflow. It is important for admin to keep track of devices with failures.
- There are use cases where EMS functions needs to be scheduled to run at certain time window.

#### Ansible Playbook Execution Flow
![Job call flow](images/fabmgt_pb_flow.png)

## 3.1 Alternatives considered
#### NA

## 3.2 API schema changes
### 3.2.1 Device and Image Management
#### device-image 
This is a new identity object added to the VNC data model to capture all the metadata for the device image. When device image object is assigned to the physical-router, the intent is to upgrade the physical-router to the specific software version specified in the device image object. When the image upgrade is successfully performed, the UvePhysicalRouterConfigTrace UVE's os_version attribute should match the one defined in the device image. Here are the list of properties for the device image object:

#### link-aggregation-group
#### Updated VNC identities:
global-system-config, physical-router, physical-interface, logical-interface, 
#### Updated UVEs:
UvePhysicalRouterConfigTrace

![Device and Image Management](images/fabmgt_dm.png)

Here are list of schema changes submitted in the review board: [https://review.opencontrail.org/#/c/38311/](https://review.opencontrail.org/#/c/38311/)

### 3.2.1 EMS Ansible Jobs
![Job Management](images/fabmgt_job_dm.png)

#### New VNC identities
job, job-template
#### New UVE and object logs
JobExecutionUVE, JobLog

## 3.3 User workflow impact
#### (pending to discussion with UI team)

## 3.4 UI changes
#### (pending to discussion with UI team)

## 3.5 Notification impact
#### Review links:
https://review.opencontrail.org/38468

# 4. Implementation
## 4.1 Device Discovery

## 4.2 Device Import

## 4.3 Image Upgrade

## 4.4 Topology Discovery

## 4.5 Underlay config

## 4.6 Ansible Modules
### VNC Ansible Module
### UVE Ansible Module
### Object Log Ansible Module

# 5. Performance and scaling impact
## 5.1 API and control plane
#### TBD

## 5.2 Forwarding performance
#### NA

# 6. Upgrade
#### backward compatible schema changes

# 7. Deprecations
#### No deprecations

# 8. Dependencies
#### Current overlay config generation in DM.

# 9. Testing
## 9.1 Unit tests
## 9.2 Dev tests
## 9.3 System tests

# 10. Documentation Impact

# 11. References
[Contrail Device Manager Design Spec](https://docs.google.com/document/d/1qVQY4N45V8AxWAXnZwvaYf5F_cuvC-9nmfS8RTHeFuU/edit#)
