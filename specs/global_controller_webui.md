Global Controller UI
===

#1. Introduction

With Contrail Global Controller, UI needs to show monitoring state for multiple
regions and sync state of objects created at global level.

#2. Problem statement

Current Contrail WebUI displays monitoring per Contrail cluster.
With Global Controller deployment, we need an ability to show status of all
Contrail clusters. We need to show alarms across Contrail Clusters.

If a config object like VN, Policy, Service Template etc is created globally
then there should be a common way to see whether the objects have been synced
across all clusters.

Queries across Messages, Objects will have to be merged by UI and displayed.

VN, VMI etc statistics data across clusters will not be merged but displayed on
a per cluster basis.

In addition, the current Gohan Virtual Network, Policy, Security Group,
Service Templates and Service Instance configuration pages are not very user
friendly. Pages from current Contrail Web UI with sync status across the
regions will be used here.

#3. Proposed solution

Contrail WebUI will add a Global Infra Dashboard that would show panels per
cluster. Each cluster will show outstanding alarms, number of
infra / down nodes, number of objects created vs synced, location, operations
VN, VMI, Instances etc.

When an admin user logs in then the user will be shown Global Infra Dashboard,
region selector will have “All Regions” selected.

Clicking on a particular panel or selecting a particular region in the region
dropdown will take to current infra dashboard with the context of the
Region/Location that has been clicked.

Network Monitoring will show list of networks created globally and their sync
status across all locations.

Any operational counters or stats for resources spread across clusters/regions
will not be aggregated. For example, In/Out counters for a Virtual Network will
be with respect to a specific cluster/region.

Query pages will have a region selector where by default the current selected
region will be used, but the user will be given an option to select more regions
and the data from multiple analytics servers will be merged by UI.

Currently when WebUI runs with Global Controller, then it enables Gohan
configuration pages when All Regions are selected from region selector. In this
mode, the VN, Security Groups, Network Policy, Service Template and
Service Instance pages from current WebUI will replace the Gohan pages.
These pages will also have support to create resources in particular location
and show the sync result.

In current implementation, Contrail WebUI logs out a user when the user changes
the Region from within UI. The session is per region and is tied to the region
specified while logging in. This behavior will be modified and the user will be
able to change regions without the need to re-login.


##3.1 Alternatives considered

Having an analytics node that would aggregate data across clusters.

For 4.0 we will have UI aggregate data across clusters.

##3.2 API schema changes
None

##3.3 User workflow impact
Described above.

##3.4 UI changes
Described above.

##3.5 Notification impact

#4. Implementation

##4.1 Work items

Change Dashboard cache jobs to be indexed per cluster. Modify the dashboard
cache and see if uve stream can help with scale requirements for vRouter uve’s.

Add panel widgets and make the landing page as Global Controller Dashboard when
running in Global Controller mode.

Add new Network Sync page for Network Monitoring.

Modify VN, SG, Policy, Service Template, Service Instance pages to be run in
Gohan mode with Create in Region and Sync status.

Add multi region drop downs in Query pages, merge data from query across
analytics nodes. Query Queue needs to show status as total for all analytics
nodes.

#5. Performance and scaling impact

Dashboard load time.

Cache implications wrt to multiple Contrail Clusters.

#6. Upgrade
None

#7. Deprecations
None

#8. Dependencies
None

#9. Testing
##9.1 Dev tests

Check that Global Controller dashboard opens only with Global Controller
endpoints in keystone.

Check that Global Controller dashboard has panels for all Regions.

Verify that we reach correct region on clicking the particular region. i.e. UI
can be in region 2, while we select region 1. In such a case UI should show
monitoring data for region2 (particular Infra Dashboard).

Check Data / status / counts in dashboard panels across all Regions.

Check load times of dashboard in standalone and Global Controller mode.

Verify that user doesn’t need to relogin with Region change.

Verify that the VN, Policy, SG, Service Template, Service Instance pages send
requests to Gohan controller for “All Regions”.

Verify that regions come in drop downs for queries. Verify that the merge of
data is fine. Verify that the Query Queue combines state for all regions.

##9.2 System tests

#10. Documentation Impact

#11. References
None
