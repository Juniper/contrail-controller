#1. Introduction

Contrail container solution is deployed as 4 containers
- controller
  This consists of following services
  - config
  - control
  - webui
- analytics
- analyticsdb
- agent 

On each host we deploy only single container type. If we need multiple instances of a container type, we install them on different nodes in a cluster.

#2. Problem statement
Analytics service depends on config, webui and analyticsdb. For some deployments, we need to deploy only analytics functionality without control service.
But current controller container has config, control and webui services in single container. There is no granular configuration to start/stop individual service in the controller container.

#3. Proposed solution

##3.1 Design
Add configuration option in the ansible inventory file such that
- No extra configuration should be needed if user wants to deploy controller with default solution i.e. all 3 services - config, control and webui are enabled in the controller container.
- On a controller container if any service is enabled explicitly, rest of the services not enabled explicitly should stay disabled.
- Config service is mandatory on a node or cluster. It is left upto the user to make sure atleast one controller container has config service enabled.
- After controller container is up, a disabled service could be started/stopped manually.
- Generic framework to be set in place such that config, control and webui services can be independently enabled/disabled for any controller container.

##3.2 Alternatives considered
Create another container, analytics+ container consisting of config, webui and analytics. For analytics-only solution, we would need analytics+ and analyticsdb containers.
This alternative was dropped as we didnt see much value is having another overlapping container. The same is being achieved by having 3 containers listed in proposed solution and
enabling/disabling the individual services in controller container as needed.

##3.3 User workflow impact
####Describe how users will use the feature.

##3.4 UI changes
None

##3.5 Notification impact
None

#4. Implementation
##4.1 Node role ansible scripts
      Read the inventory file for [contrail-controller] hosts. List of IP addresses is controller_list.
      If controller-components are not defined set config_server_list, control_server_list and webui_server_list as controller_list.
      Else, assign IP addresses to the repective lists as per configuration.
      Pass these parameters to controller containers.

##4.2 Contrail role - config, control, webui service ansible scripts
      In controller container, start the services only if it is enabled.
      Start/stop rabbitmq, cassandra and zookeeper only if config service is enabled.

#5. Performance and scaling impact
##5.1 API and control plane
None

##5.2 Forwarding performance
None

#6. Upgrade
None

#7. Deprecations
None

#8. Dependencies
None

#9. Testing
##9.1 Unit tests
1. Default inventory file configuration for controller container
- single node
[contrail-controllers]
10.84.32.10

[contrail-analyticsdb]
10.84.32.10

[contrail-analytics]
10.84.32.10

- multi node cluster
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

2. analytics+ solution inventory file configuration for controller container
- single node
[contrail-controllers]
10.84.32.10     controller_components=['config','webui']

[contrail-analyticsdb]
10.84.32.10

[contrail-analytics]
10.84.32.10

- multi node cluster
[contrail-controllers]
10.84.32.10     controller_components=['config','webui']
10.84.32.11     controller_components=['config','webui']
10.84.32.12     controller_components=['config','webui']

[contrail-analyticsdb]
10.84.32.10
10.84.32.11
10.84.32.12

[contrail-analytics]
10.84.32.10
10.84.32.11
10.84.32.12

##9.2 Dev tests
##9.3 System tests

#10. Documentation Impact

#11. References
