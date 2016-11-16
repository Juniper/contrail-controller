Contrail 4.0 Removal of Discovery Service
===
#1.      Introduction

Contrail Discovery Service is a centralized resource allocation module with high
availability that was primarily developed to

* auto load balance resources in the system.
* Register(=publish) new resources directly with the Contrail Discovery Module
  to be allocated to the requester(=subscriber) of the resource service
  without disrupting the running state of the subscribers.

The above necessitated the use of a database
* To synchronize across Contrail Discovery nodes.
* To maintain the list of publishers, subscribers and health of the
  published services across reloads.
* Provide Centralized view of the service allocation and health of the
  services.


#2.      Problem Statement

In the current deployment most issues seen are due to the periodic health
updates of services with the database nodes and not the services themselves.
This would result in services being falsely marked DOWN resulting in
reallocation of healthy services causing unnecessary churn in the system.

Hence the motivation to move away from a Centralized Resource Allocation Manager
and bringing up each module with a list of pre-defined service
providers(=publishers).


#3.      Proposed Solution

Details of Implementation of Distributed Resource Allocation

* Each module is provisioned with a list of service nodes(=publishers).
* Each module will randomize this list of service nodes and use the resources,
  this randomized list is expected to be fairly load balanced.
* When currently used services are DOWN, the module detects it immediately and
  can react with no downtime by picking another service from the list.
  (as opposed to contacting the Discovery Server for services in which case
   there is a finite time loss for allocation, distribution and application
   of new set of services).
* When service nodes are added or deleted, ADMIN will need to update the
  configuration file of all daemons using the service-type of the service node
  and send a SIGHUP to the respective daemons.
* Each daemon will randomize the service list independently and re-allocate the
  resources.

##3.1      Alternatives Considered
None

##3.2      API schema changes
None

###3.2.1   Provisioning Changes for contrail-vrouter-agent(contrail-vrouter-agent.conf)
        
```
================================================================================
 CONFIG PARAMETER          CHANGES
================================================================================
[DISCOVERY].server         Deprecate Discovery Server Parameter

[CONTROL-NODE].servers     Provision list of control-node service providers
                           in ip-address:port format
                           Eg: 10.1.1.1:5269 10.1.1.12:5269

[DNS].servers              Provision list of DNS service providers in
                           ip-address:port format
                           Eg: 0.1.1.1:53 10.1.1.2:53

[DEFAULT].collectors       Provision list of Collector service providers in
                           ip-address:port format
                           Eg: 10.1.1.1:8086 10.1.1.2:8086

```

###3.2.2   Provisioning Changes for contrail-control (contrail-control.conf)

```
================================================================================
 CONFIG PARAMETER          CHANGES
================================================================================
[DISCOVERY].server         Deprecate Discovery Server Parameter

[DEFAULT].collectors       Provision list of Collector service providers in
                           ip-address:port format
                           Eg: 10.1.1.1:8086 10.1.1.2:8086

[IFMAP].servers            Provision list of IFMap service providers in
                           ip-address:port format
                           Eg: 10.1.1.1:8443 10.1.1.2:8443
```

###3.2.3   Provisioning Changes for contrail-dns (contrail-control.conf)

```
================================================================================
 CONFIG PARAMETER          CHANGES
================================================================================
[DISCOVERY].server         Deprecate Discovery Server Parameter

[DEFAULT].collectors       Provision list of Collector service providers in
                           ip-address:port format
                           Eg: 10.1.1.1:8086 10.1.1.2:8086

[IFMAP].servers            Provision list of IFMap service providers in
                           ip-address:port format
                           Eg: 10.1.1.1:8443 10.1.1.2:8443
```

###3.2.4   Provisioning Changes for contrail-alarm-gen (contrail-alarm-gen.conf)

```
================================================================================
 CONFIG PARAMETER          CHANGES
================================================================================
[DISCOVERY].server         Deprecate Discovery Server Parameter

[DEFAULT].collectors       Provision list of Collector service providers in
                           ip-address:port format
                           Eg: 10.1.1.1:8086 10.1.1.2:8086
```

###3.2.5 Provisioning Changes for contrail-analytics-api(contrail-analytics-api.conf)

```
================================================================================
 CONFIG PARAMETER               CHANGES
================================================================================
[DISCOVERY].disc_server_ip      Deprecate Discovery Server Parameter
[DISCOVERY].disc_server_port

[DEFAULT].collectors            Provision list of Collector service providers in
                                ip-address:port format
                                Eg: 10.1.1.1:8086 10.1.1.2:8086

```

###3.2.6  Provisioning Changes for contrail-api (contrail-api.conf)

```
================================================================================
 CONFIG PARAMETER               CHANGES
================================================================================
[DISCOVERY].disc_server_ip      Deprecate Discovery Server Parameter
[DISCOVERY].disc_server_port

[DEFAULTS].collectors           Provision list of Collector service providers in
                                ip-address:port format
                                Eg: 10.1.1.1:8086 10.1.1.2:8086
```

###3.2.7 Provisioning Changes for contrail-api (contrail-schema.conf)

```
================================================================================
 CONFIG PARAMETER               CHANGES
================================================================================
[DISCOVERY].disc_server_ip      Deprecate Discovery Server Parameter
[DISCOVERY].disc_server_port

[DEFAULTS].collectors           Provision list of Collector service providers in
                                ip-address:port format
                                Eg: 10.1.1.1:8086 10.1.1.2:8086
```

###3.2.8 Provisioning Changes for contrail-svc-monitor(contrail-svc-monitor.conf)

```
================================================================================
 CONFIG PARAMETER               CHANGES
================================================================================
[DISCOVERY].disc_server_ip      Deprecate Discovery Server Parameter
[DISCOVERY].disc_server_port

[DEFAULTS].collectors           Provision list of Collector service providers in
                                ip-address:port format
                                Eg: 10.1.1.1:8086 10.1.1.2:8086

```

###3.2.9 Provisioning Changes for contrail-analytics-nodemgr(contrail-analytics-nodemgr.conf)

```
================================================================================
 CONFIG PARAMETER          CHANGES
================================================================================
[DISCOVERY].server         Deprecate Discovery Server Parameter
[DISCOVERY].port

[COLLECTOR].server_list    Provision list of Collector service providers in
                           ip-address:port format
                           Eg: 10.1.1.1:8086 10.1.1.2:8086
````


###3.2.10  Provisioning Changes for contrail-config-nodemgr(contrail-config-nodemgr.conf)

```
================================================================================
 CONFIG PARAMETER          CHANGES
================================================================================
[DISCOVERY].server         Deprecate Discovery Server Parameter
[DISCOVERY].port

[COLLECTOR].server_list    Provision list of Collector service providers in
                           ip-address:port format
                           Eg: 10.1.1.1:8086 10.1.1.2:8086
```

###3.2.11 Provisioning Changes for contrail-control-nodemgr(contrail-control-nodemgr.conf)

```
================================================================================
 CONFIG PARAMETER          CHANGES
================================================================================
[DISCOVERY].server         Deprecate Discovery Server Parameter
[DISCOVERY].port

[COLLECTOR].server_list    Provision list of Collector service providers in
                           ip-address:port format
                           Eg: 10.1.1.1:8086 10.1.1.2:8086
```

###3.2.12  Provisioning Changes for contrail-database-nodemgr(contrail-database-nodemgr.conf)

```
================================================================================
 CONFIG PARAMETER          CHANGES
================================================================================
[DISCOVERY].server         Deprecate Discovery Server Parameter
[DISCOVERY].port

[COLLECTOR].server_list    Provision list of Collector service providers in
                           ip-address:port format
                           Eg: 10.1.1.1:8086 10.1.1.2:8086
```

###3.2.13 Provisioning Changes for contrail-vrouter-nodemgr(contrail-vrouter-nodemgr.conf)

```
================================================================================
 CONFIG PARAMETER          CHANGES
================================================================================
[DISCOVERY].server         Deprecate Discovery Server Parameter
[DISCOVERY].port

[COLLECTOR].server_list    Provision list of Collector service providers in
                           ip-address:port format
                           Eg: 10.1.1.1:8086 10.1.1.2:8086
```

###3.2.14 Provisioning Changes for contrail-query-engine(contrail-query-engine.conf)

```
================================================================================
 CONFIG PARAMETER          CHANGES
================================================================================
[DISCOVERY].server         Deprecate Discovery Server Parameter
[DISCOVERY].port

[DEFAULT].collectors       Provision list of Collector service providers in
                           ip-address:port format
                           Eg: 10.1.1.1:8086 10.1.1.2:8086

```

###3.2.15 Provisioning Changes for contrail-snmp-collector(contrail-snmp-collector.conf)

```
================================================================================
 CONFIG PARAMETER               CHANGES
================================================================================
[DISCOVERY].disc_server_ip      Deprecate Discovery Server Parameter
[DISCOVERY].disc_server_port

[DEFAULT].collectors            Provision list of Collector service providers in
                                ip-address:port format
                                Eg: 10.1.1.1:8086 10.1.1.2:8086
```

###3.2.16  Provisioning Changes for contrail-topology (contrail-topology.conf)

```
================================================================================
 CONFIG PARAMETER               CHANGES
================================================================================
[DISCOVERY].disc_server_ip      Deprecate Discovery Server Parameter
[DISCOVERY].disc_server_port

[DEFAULT].collectors            Provision list of Collector service providers in
                                ip-address:port format
                                Eg: 10.1.1.1:8086 10.1.1.2:8086

```

###3.2.17  Provisioning Changes for ContrailWebUI

```
================================================================================
 CONFIG PARAMETER                 CHANGES
================================================================================
[CONFIG].discovery.server         Deprecate Discovery Server Parameter
[CONFIG].discovery.port

[CONFIG].analytics.servers        Provision list of Analytics service providers
                                  in ip-address:port format
                                  Eg: 10.1.1.1:8089 10.1.1.2:8089

[CONFIG].apiserver.servers        Provision list of ApiServer service providers in
                                  ip-address:port format

[CONFIG].dns.servers              Provision list of DnsServer service providers in
                                  ip-address:port format
```

##3.3      User workflow impact

* Provisioning will need to take care of adding the published service list.
* Discovery Server parameter will be deprecated.

##3.4      UI Changes
None


#4 Implementation

Each daemon will randomize the published service list that is configured
statically and use the resources. In addition each daemon will provide SIGHUP
handler to  handle addition/deletion of publishers.

##4.1     Assignee(s)

* Nipa Kumar – All C++ Discovery Server Clients
* Santosh G –  All python Discovery Server Clients

#5 Performance and Scaling Impact
None

##5.1     API and control plane Performance Impact
None

##5.2     Forwarding Plan Performance
None

#6 Upgrade
Discovery Server will need be deprecated from the configuration files.

#7       Deprecations
Discovery Server will be deprecated.

#8       Dependencies
None

#9       Testing

* Individual daemons will need to brought up via provisioning and ensure a fairly
  load balanced allocation.
* SIGHUP will also need to be sent when service nodes are added/deleted.

#10      Documentation Impact
None

#11      References
None
