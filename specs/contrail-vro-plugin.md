# 1. Introduction
This document describes overview of the OpenContrail plugin for vRealize Orchestrator developed as part of the Contrail vRealize project.

# 2. Problem statement
The purpose of this development is to connect OpenContrail to VMware vRealize Orchestrator (vRO). This is required for OpenContrail to be used in an environment where vRO is used for automation of management processes in data centers.

# 3. Proposed solution
OpenContrail can be connected to vRO using a dedicated plugin. VMware provides SDK and API for developing plugins for vRealize Orchestrator. Such plugins can be implemented in any Java Virtual Machine (JVM) compatible language and loaded into a running instance of vRO. The plugin will allow to view OpenContrail controller configuration in the vRO inventory. It will also allow for modifications of this configuration using vRO workflows.
## 3.1 Alternatives considered
Creating a vRO plugin is the only technique for connection vRO with other systems.
Usage of JVM compatible languages is necessary since vRO is itself a Java application running on Tomcat server and does not provide any other runtime environment.

## 3.2 API schema changes
Project does not require any API schema changes. 
Minor changes to OpenContrail Java API are required. This changes are necessary because vRO and OpenContrail Java API use two incompatible versions of Gson library, which is used for JSON serialization. Required changes will include an update to the latest Gson version and correction to the way unsigned long integers are serialized.

## 3.3 User workflow impact
This feature does not impact user workflow.

## 3.4 UI changes
This feature does not impact UI.

## 3.5 Notification impact
This feature does not impact the notification system.

# 4. Implementation
This feature will be implemented as a Maven project for which archetype is provided by VMware. Most of the plugin code will consist of a generator that creates vRO objects based on Contrail schema.

# 4.1 Work items
1. Create infrastructure code that allows for connection to the vRO.
2. Implement connection to a Contrail controller.
3. Based on Contrail schema and Java API create generator of vRO inventory that displays the state of Contrail controller.
4. Based on Contrail schema and Java API create generator of vRO workflows that manipulate the state of contrail controller.
5. Customize vRO inventory view and workflows for most typical use cases.

# 5. Performance and scaling impact
## 5.1 API and control plane
This feature does not impact API and control plane.

## 5.2 Forwarding performance
This feature does not impact forwarding performance.

# 6. Upgrade
Most of the plugin functionality will generated based on Contrail schema and Java API and should not be sensitive to changes in the schema. However parts of the plugin that will be custom made, instead of generated, may require modification if schema changes significantly.

# 7. Deprecations
This feature does not deprecate any older feature.

# 8. Dependencies
This feature depends on OpenContrail Java API.

# 9. Testing
Significant part of the final plugin code will be generated and should be mainly tested at the level of system tests.

## 9.1 Unit tests
Development will include unit tests for non-generated code included in the plugin.

## 9.2 Dev tests
Development will include code review. 

## 9.3 System tests
System tests will be validating two main parts of the generated vRO plugin. The first one will be proper representation of the state of the Contrail controller in the vRO inventory. The second one will be proper execution of the workflows which should modify the state of Contrail controller. System tests will will require loading plugin into a vRO instance and executing workflows through the vRO REST API. Validation of the changes applied to Contrail controller will be performed using Contrail REST API.

# 10. Documentation Impact
TODO

# 11. References
1.  Developing with VMware vRealize Orchestrator: https://docs.vmware.com/en/vRealize-Orchestrator/7.3/com.vmware.vrealize.orchestrator-dev.doc/GUID-B5C0EE02-0E6B-4625-826C-47CD5323488B.html
2.  vRealize Orchestrator Coding Design Guide: https://docs.vmware.com/en/vRealize-Orchestrator/7.3/vrealize_orchestrator_coding_design_guide.pdf
3.  vRealize Orchestrator Plug-In Development Guide: https://docs.vmware.com/en/vRealize-Orchestrator/7.0/vrealize_orchestrator_plug-in_development_guide.pdf
4.  Plug-In SDK Guide for vRealize Orchestrator: https://docs.vmware.com/en/vRealize-Orchestrator/7.0/plug-in_SDK_guide_for_vrealize_orchestrator.pdf
