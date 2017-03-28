# 1. Introduction

Contrail controller container solution is deployed as the following 4 containers
- controller
  This consists of following services
  - config
  - control
  - webui
- analytics
- analyticsdb
- vrouter-agent

A typical HA solution will have 3 hosts each having controller, analytics & analyticsdb containers on it.

# 2. Problem statement
Contrail Analytics should be installable as a standalone product independent of Contrail Controller SDN solution. Standalone Contrail Analytics should have config, webui, analytics and analyticsdb services. analytics and analyticsdb have their own containers. config & webui will come from controller container, but this requires controller container to be deployed with control service disabled.

# 3. Proposed solution

## 3.1 Design
Add configuration option in the ansible inventory file such that
- No extra configuration should be needed if user wants to deploy controller with default solution i.e. all 3 services - config, control and webui are enabled in the controller container.
- On a controller container if any service is enabled explicitly, rest of the services not enabled explicitly should stay disabled.
- Config service is mandatory on a node or cluster. It is left upto the user to make sure atleast one controller container has config service enabled.
- After controller container is up, a disabled service could be started/stopped manually.
- Generic framework to be set in place such that config, control and webui services can be independently enabled/disabled for any controller container.

## 3.2 Alternatives considered
Create another container, analytics+ container consisting of config, webui and analytics. For analytics-only solution, we would need analytics+ and analyticsdb containers.
This alternative was dropped as we didnt see much value is having another overlapping container. The same is being achieved by having 3 containers listed in proposed solution and
enabling/disabling the individual services in controller container as needed.

## 3.3 User workflow impact
Describe how users will use the feature.

## 3.4 UI changes
We have to make sure non existence of control and vrouter services will not create issues on the UI

## 3.5 Notification impact
None

# 4. Implementation
## 4.1 Node role ansible scripts
Read the inventory file for [contrail-controller] hosts. List of IP addresses is controller_list. If controller-components are not defined or empty, set config_server_list, control_server_list and webui_server_list as controller_list. If controller_components is non-emtpy, assign IP addresses to the respective lists as per configuration. Pass these parameters to controller containers.

## 4.2 Contrail role - config, control, webui service ansible scripts
In controller container, start the services only if they is enabled. As rabbitmq, cassandra and zookeeper are used by config only, they are enabled only if config service is enabled.

# 5. Performance and scaling impact
## 5.1 API and control plane
None

## 5.2 Forwarding performance
None

# 6. Upgrade
None

# 7. Deprecations
None

# 8. Dependencies
None

# 9. Testing
## 9.1 Unit tests
### 9.1.1 default inventory file configuration for controller container

```
; single node
[contrail-controllers]
10.84.32.10

[contrail-analyticsdb]
10.84.32.10

[contrail-analytics]
10.84.32.10
```

```
; multi node cluster
[contrail-controllers]
10.84.32.10
10.84.32.11
10.84.32.12

[contrail-analyticsdb]
10.84.32.10
10.84.32.11
10.84.32.12

[contrail-analytics]
10.84.32.10
10.84.32.11
10.84.32.12
```

### 9.1.2 analytics standalone solution inventory file configuration for controller container

```
; single node
[contrail-controllers]
10.84.32.10             controller_components=['config','webui']

[contrail-analyticsdb]
10.84.32.10

[contrail-analytics]
10.84.32.10
```

```
; multi node cluster
[contrail-controllers]
10.84.32.10             controller_components=['config','webui']
10.84.32.11             controller_components=['config','webui']
10.84.32.12             controller_components=['config','webui']

[contrail-analyticsdb]
10.84.32.10
10.84.32.11
10.84.32.12

[contrail-analytics]
10.84.32.10
10.84.32.11
10.84.32.12
```

## 9.2 Dev tests
## 9.3 System tests

# 10. Documentation Impact

# 11. References


