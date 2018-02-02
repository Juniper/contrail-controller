
# 1. Introduction
     The document goes over the requirement to have different TTL
     for different stats stored in cassandra. The TTL dictates
     how long the data resides in cassandra.
# 2. Problem statement
     The data sent by UVE's can be parsed for tags and stats can
     be collected for the stats. Today the TTL for all the stats
     is same and is specified in contrail-collector.conf
     Currently, we cannot change the TTL's for different stats table
     while running.
# 3. Proposed solution
#### Describe the proposed solution.
     Make the TTL configurable and expose it through api-server.
     Upon recieving updates, make the collector change the TTL's
     of the tables.
## 3.1 Alternatives considered
#### Describe pros and cons of alternatives considered.

## 3.2 API schema changes
     New schema is added to specify the TTL's. It lets one to
     set the tables TTL in hours. A dictionary is provided with
     key as the table name and value as TTL in hours.
     
## 3.3 User workflow impact
     TTL's will be intially set by collector based on the values
     specified in collector.conf. It update the entries in
     config database.
     Any updates made can be done thorugh api's or UI and collector
     updates the config database with new values.
     To fnd the virtual stats tables, please visit analtics-api 
     port and look for table names with StatTable.*
     Eg., 10.84.13.9:8081/analytics/tables/StatTable.NodeStatus.*
## 3.4 UI changes
     New window to let the user configure TTL will be added.
## 3.5 Notification impact
     Log will be added indicating whenever TTL's were updated.

# 4. Implementation
## 4.1 Work items
     A new schema is added to define the configurable TTL. A new
     object is created anchored under global-system-config. A map
     property is added with key as table name and value as TTL in hours.
     Table name can be StatTable....
     Collector will add a new listener to handle TTL updates. This
     listener will be hooked to the existing config-client-collector.
     Upon recieving a notification, collector updates a map currently
     being used for storing TTL.
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
