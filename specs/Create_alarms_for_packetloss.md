
# 1. Introduction
Currently vRouter , VMI and Virtual Network UVE provide Packet Loss details(from Contrail 3.2). Due to unavailability of Python ruleset, Packet Loss alarm is not getting triggered.We are proposing to create new packet loss alarm in OPSERVER for Virtual Machine Interface(VMI), VRouter and Virtual Network(VN) by using dropstats statistics. Downstream applications such as Contrail GUI can use Opserver API to pull Packet loss statistics.


# 2. Problem statement
Currently vRouter captures Packet Loss counters by following UVEs. Due to unavailability of Python ruleset, Packet Loss alarm is not getting triggered. Below are the agents for each UVE.
1. VrouterStatsAgent for vRouter
2. UveVirtualNetworkAgent for Virtual Network
3. UveVMInterfaceAgent for VirtualMachineInterface


# 3. Proposed solution
Create new alarms to detect packet loss . This will help in detecting failures early and can be rectified without any major issues.
dropstats percentage will be calculated using the formula: drop_pkts_percent = drop_pkts/( in_pkts + out_pkts).
     If drop_pkts_percent > 1% then notification will be raised and response will be triggered.
     Notification and response needs to be defined.

Use cases – Identified 7 fields in dropstats output for which alarm needs be created
Virtual Machine Interface , Vrouter, Physical Interface.

## 3.1 Alternatives considered
User can execute dropstats command manually and check or user can check values of Analytics API’s manually.

## 3.2 API schema changes
#### Describe api schema changes and impact to the REST APIs.

## 3.3 User workflow impact
Automatically alarms are created when python coded rules are satisfied.

## 3.4 UI changes
No UI Changes. Through UI also we can create alarms.

## 3.5 Notification impact
New alarms are created. Logs will be added when alarms are raised. No UVE Changes.


# 4. Implementation
## 4.1 Work items

Changes in Opserver:

New plugins are added in opserver for packet loss alarms.
Install python plugin for an alarm on Analytics node.
Plugins are added in the folder : controller/src/opserver/plugins/
Implementation of the plugin is in (alarm_packet_loss/main.py).Python coded rules are added in the main.py files of the corresponding plugins.
Plugins created are added in Sconscript.
Python coded rule will check the dropstats values to the values in message table of the corresponding UVE of Virtual Machine Interface, VRouter and Virtual Network.
Create entry points in setup.py for the corresponding plugins.


# 5. Performance and scaling impact
## 5.1 API and control plane
#### Scaling and performance for API and control plane

## 5.2 Forwarding performance
#### Scaling and performance for API and forwarding

# 6. Upgrade
#### Describe upgrade impact of the feature
#### Schema migration/transition

# 7. Deprecations
#### If this feature deprecates any older feature or API then list it here.

# 8. Dependencies
#### Describe dependent features or components.

# 9. Testing
## 9.1 Unit tests
## 9.2 Dev tests
## 9.3 System tests

# 10. Documentation Impact

# 11. References
