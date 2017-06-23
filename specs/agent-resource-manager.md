
# 1. Introduction
  This is to make sure same resources are allocated after restart.
  These are the resources allocated locally by agent.
  One of the resource which is visible across system is MPLS label.

# 2. Problem statement
  On restart if agent sends new label for local routes then it will cause change
  in whole system. Presence of resource manager makes sure that same label is
  givenand hence system does not see any change.

# 3. Proposed solution
* Allocator
  It is responsible for identifying what kind of resource is needed and manages
  the users claims on them. Each user specifies a key and allocator allocate
  one resource to same. This key to resource mapping is stored in manager as a map.
  Keys can be heterogenous depending on users. For example MPLS label can have
  users like VRF, VMI, Multicast. Each of them can have different key.
  Currently index vector has been made as a resource, 
  but more can be added as and when needed.

* Backup
 Each resource allocated above has an option to be backed up. For backup the
 resource key and data is converted to sandesh format. In case of MPLS above
 each type of user key will have its sandesh representation and mpls label
 will be represented as one more sandesh. The key data pair is then pushed
 into a sandesh map which is then written to a file. Each resource update
 keeps on modifying this map. Point to note update does not result in file
 operation. There is a separate timer which is responsible for dumping all
 backed up resource to file. On restarting these files will be read and 
 resource manager populated before any config is processed. This makes sure
 that same data is allocated for a key requested by config and hence persistence
 is achieved. Though backup module is flexible to not only take data to be
 backed up from resource manager but from any other source as well. 
 All it needs is a sandesh representation of same and consumer once restore is done.

* This will be further extended to back up config later. 
  Other extensions which have to be done are agent resources like nexthop-id, 
  vrf-id, i/f-id etc.
### Caveats

## 3.1 Alternatives considered
None

## 3.2 API schema changes

## 3.3 User workflow impact
Agent conf file needs to be udated with Backup flag enable so that data will be
serilaized and stored in a file.
Upon reboot  Vrouter agent will read the data from the file and will restores the
data.
## 3.4 UI changes

## 3.5 Notification impact

# 4. Implementation
## 4.1 Work items
## 4.1.2 ResourceManager:
* This class is responsible for restore and retaining the config's.
  It also triggers the resource allocation by using the key defind by the feature.
  Maintains the Restore WorkQueue to restore the data from file.
  Restore the data from the file and creates association of Resource Key & data.
  And marks the Key as dirty.  Once after receving the Config validates the Resource key
  and mark the Key as usable if it exists in config.
  Once after downloading the config, EOD RIB Timer notification will come.
  After the notification walk through all the entries and remove the  stale.

## 4.1.3 ResourceTable:
   * It maintains Resource Key & data Map table. Resource Manager requests, 
   resource table  object to create & delete the resources based on type of the resources.

## 4.1.4 IndexResourceTable:
 * Maintains the index resource vector for resourcekey.
   For every resource key  a unique index will be allocated which will be
   represented as label or it can be represented as respective
   resource Index, This Resource Index will also be used to represent the stored  data
   in the file. Index will be mapped back to index vector after reading from the file.

## 4.1.5 ResourceBackupManager:
 * Maintains the WorKQueue for backup data.
   All the features which needs to back up the data can enqeue data to this work queue.
   ResourceBackupManager reads the data from the WorkQueue and Triggers the data backup to file

## 4.1.6 BackUpResourceTable:
* Backup Resource Table is a base class. every feature class wants to backup the data
  Needs to derive from this class. each of this class maintains table.
  contains resource index and Sandesh structure.

* Timer is maintained per Backup Resource table.
  will be used to trigger Write to file based on idle time out logic.
  Trigger will be intiated Only when we don't see any frequent Changes.

* In case of modification with in the range of idle time out.
  Write will be postponed to next time slot, otherwise Write to file will happens upon
  fallback time period.

# 5. Performance and scaling impact
## 5.1 API and control plane
None

## 5.2 Forwarding performance

# 6. Upgrade
No impact. 

# 7. Deprecations
None

# 8. Dependencies
None

# 9. Testing
## 9.1 Unit tests
* Create the VM Interface and check that corresponding mpls labels like interface,
vrf, route labels are stored in the file.
* Restart the Vrouter make sure that labels are retained.

## 9.2 Dev tests
* Create Mpls resource labels and check that labels are stored in to file
* Delete those MPLS labels and make sure that labels are deleted and stored in
  to the file.A
* Check labels are retained upon multiple restarts
* Check that traffic is not effected upon restarts

## 9.3 System tests
* End to end testing  After restoring the lables make sure that Traffic is not
  effected.
* Created new VM after the restart and check the traffic is fine between new VM
  and existing VM'sA

# 10. Documentation Impact

# 11. References
https://github.com/Juniper/contrail-controller/wiki/Agent-Resource-Manager
