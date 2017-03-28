Proxy UVEs in Contrail Analytics
===
# 1.      Introduction

Contrail Analytics provides system operational state as UVEs, and allows
for alarms based on the state of an object. We also need to provide
streaming-based statistics on metrics of these objects, and alarms based
on these statistics.

This has been done for UVEs that do not require aggregation, such as 
Node UVEs and VMI UVEs, by the DerivedStats feauture. But this still needs
to be implemented for Virtual Networks (which DO require aggegation)


# 2.      Problem Statement

We have two places where UVE aggregation is being done
* In contrail-alarm-gen on the Analytics Node : This aggregates any UVE,
  and evauluates for alarms. This Python code does state-compression when
  there is high load, so it is not a good fit for processing Statistics streams.
* In the SandeshUVE cache of contrail-vrouter-agent, contrail-control and
  contrail-collector itself, where we can aggregate using DerivedStats.
  DerivedStats. By distrbuting the processing to client processes, we can
  process Statistics streams in a scalable way, but the aggregations are 
  across individual process instances only.

The Virtual Network is the most significant abstraction in the Contrail
System, and we need to do streaming Statistics and Alarms for these
UVEs. These objects span across many vrouters. We will look at 
implementation Anomaly Detection on the total number of flows in a 
Virtual Network, and this algorithm must be run in a single place 
for a given VN.

# 3.      Proposed Solution

User will ask Contrail Analytics to do aggregations (including anomaly
detection) on specific raw metrics. This aggregation will happen inside
contrail-collector, on a per-object basis.

Aggregations can be configured in the field, though there will be a fixed
menu of algorithms available. (Adding new algorithms needs code change.)

The results of these aggregations will be added to the original UVE (against
the same key). The user can ask for multiple aggregations against the
same raw metric. (E.g. run two anomaly detections with different
parameters, or , in future, run another summarization such as
percentiles while also running one or more Anomaly Detections)

The user can set alarms on these results as needed using existing
mechanisms, and Stats will be available as well.

All this will be implemented in the form of Proxe UVE structures, which
contrail-collectors will create.

## 3.1      Proxy UVE operation

Here is an example of a proxy UVE structure:

```
struct AggProxySumAnomalyEWM01 {
  1: string name (key="")
  /* e.g. proxy="UveVirtualNetworkAgent-ingress_flow_count" */
  2: string proxy
  3: optional bool deleted
  4: optional u64 raw (hidden="yes")
  5: optional derived_stats_results.AnomalyResult value (stats="1.DSSum-raw:DSAnomaly:EWM:0.1", tags="proxy")
} (period="60", timeout="1")
```

This UVE will run periodic DerivedStats. We take the SUM of the raw metric
accumulated every 60 seconds, and feed it into the EWM Anomaly Detection 
algorithm, with alpha value of 0.1.

Scince we have a "timeout" of 1, we will delete this proxy UVE if there are no
updates to this raw metric for 60 seconds.

The "proxy" attribute holds the UVE struct and attribute of the raw metric.
This same proxy UVE type can be reused for multiple raw metrics against 
the same UVE. (e.g. UveVirtualNetworkAgent-ingress_flow_count and
UveVirtualNetworkAgent-egress_flow_count)

This is how these proxy UVE Structures will show up in a given
Virtual Network UVE:

```
UveVirtualNetworkAgent: {...}
UveVirtualNetworkConfig: {...}
ContrailConfig: {...}
RoutingInstanceStatsData: {...}
AggProxySumAnomalyEWM01-UveVirtualNetworkAgent-egress_flow_count: {
    deleted: false,
    proxy: "UveVirtualNetworkAgent-egress_flow_count",
    value: {
        config: "0.1",
        metric: 36,
        state: {
            stddev: "0.000172333",
            mean: "36"
        },
    algo: "EWM",
    samples: 235,
    sigma: 0.00000764143
    }
},
AggProxySumAnomalyEWM01-UveVirtualNetworkAgent-ingress_flow_count: {
    deleted: false,
    proxy: "UveVirtualNetworkAgent-ingress_flow_count",
    value: {
        config: "0.1",
        metric: 31,
        state: {
            stddev: "0.000472838",
            mean: "29"
        },
    algo: "EWM",
    samples: 235,
    sigma: 0.1000764888
    }
},

```


## 3.2      Proxy UVE configuration

Which Aggregations to run on which raw metrics will be configurable via
contrail-collector.conf, using the "DEFAULT.uve_proxy_list" option.

(In future, we will add support for configuring
this via the VNCApi)

As shown in the  example below, by default, two aggregation algorithms
(AggProxySumAnomalyEWM01 and AggProxySum) are being run against two
raw metrics (UveVirtualNetworkAgent-ingress_flow_count and
UveVirtualNetworkAgent-egress_flow_count)

```
#contrail-collector --help
Allowed options:

Generic options:
  --conf_file arg (=Configuration file)
  --help                                help message
  --version                             Display version information

Configuration options:
...
  --DEFAULT.uve_proxy_list arg (=UveVirtualNetworkAgent-egress_flow_count:AggProxySumAnomalyEWM01:AggProxySum UveVirtualNetworkAgent-ingress_flow_count:AggProxySumAnomalyEWM01:AggProxySum)
                                        UVE Proxy List
...

```
# 3.3      User workflow impact
None

## 3.4      UI Changes
None


# 4 Implementation

The contrail-collectors will put UVE updates for VNs on kafka topics,
(multiple collectors might be receiving updates from different vrouters
for the same VN), and then read them back on a per-VN basis. (all updates
for a given VN can be processed on the same collector). Kafka offers the
Consumer-Group feature, using which a set of consumers can distribute
a given partitioned topic (according to a key)  amongst themselves, 
and kafka will load-balance automatically as consumers go up and down.

The contrail-collectors use DerivedStats to aggregate a raw metric (e.g.
number of egress flows) on a per-VN basis, and then feed the aggregate
into a the EWM Anomaly Detection algorithm.

## 4.1     Assignee(s)

* Anish Mehta

# 5 Performance and Scaling Impact
None

## 5.1     API and control plane Performance Impact
None

## 5.2     Forwarding Plane Performance
None

# 6 Upgrade
None

# 7       Deprecations
None

# 8       Dependencies
None

# 9       Testing
None

# 10      Documentation Impact
None

# 11      References
None
