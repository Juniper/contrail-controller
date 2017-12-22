
# 1. Introduction
#### Contrail Fabric management use cases require Contrail to provide more EMS capabilities on physical network elements, such as TOR/EOR switches, Spines, SDN gateway, and VPN gateways in the data center. User should be able to perform basic device management functions such device image upgrade, device inventory discovery, RMA, etc.  

# 2. Problem statement
#### Contrail currently does not fully manage the underlay network in the data center. As we expand Contrail responsibility to both overlay and underlay network orchestration and automation, we need more EMS FCAPS capabilities to be added Contrail. This document focuses on the design of some basic EMS functions, such as device discovery and image upgrade. Because device discovery and image upgrade are usually initiated by end user and may take long time to complete, we also need to add job support in Contrail so that user can run these functions as a background job and get notified when job is finished. 

#### User Stories:
- [GLAC-2: Importing/Discovering DC Fabric and Service Objects](https://aspg-jira.juniper.net/browse/GLAC-2)
- [GLAC-7: DC Fabric and Service objects software upgrade](https://aspg-jira.juniper.net/browse/GLAC-7)
- [GLAC-47: Device Onboarding - brown field](https://aspg-jira.juniper.net/browse/GLAC-47)
- [GLAC-136: Device Topology Discovery](https://aspg-jira.juniper.net/browse/GLAC-136)

# 3. Proposed solution
#### The proposed solution is captured in a separate document listed below:
[Contrail Device Manager Design Spec](https://docs.google.com/document/d/1qVQY4N45V8AxWAXnZwvaYf5F_cuvC-9nmfS8RTHeFuU/edit#)

## 3.1 Alternatives considered
#### NA

## 3.2 API schema changes
#### Here are list of schema changes submitted in the review board: 
 - Image management and device discovery schema changes: [https://review.opencontrail.org/#/c/38311/](https://review.opencontrail.org/#/c/38311/)
 - TBD: job schema changes

## 3.3 User workflow impact
#### (pending to discussion with UI team)

## 3.4 UI changes
#### (pending to discussion with UI team)

## 3.5 Notification impact
#### Review links:
https://review.opencontrail.org/38468

# 4. Implementation
## 4.1 Work items
- Ansible plugin infrastructure 
- Ansible Playbook related work items:
    - add Ansible module to invoke Sandesh APIs
    - add Ansible module to invoke VNC APIs
    - add Ansible Playbook for device discovery
    - add Ansible Playbook for inventory discovery
    - add Ansible Playbook for device image upgrade
    - add Ansible Playbook for lldp based topology discovery
    - add Ansible Playbooks for underlay configs

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
- [Contrail Fabric Design](https://docs.google.com/document/d/1IGbpHSuPQ1lyDQ36fXGBAqTgKgyFJtME60Qh0iPV4fg/edit)
- [Contrail Device Manager Design Spec](https://docs.google.com/document/d/1qVQY4N45V8AxWAXnZwvaYf5F_cuvC-9nmfS8RTHeFuU/edit#)
- [Using Contrail Fabric - Epic Story](https://drive.google.com/file/d/0B6B1IUbh4KkrMFRDR1l1TnhpNW8/view)
