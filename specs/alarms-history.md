
# 1. Introduction
Contrail solution sends alarms and UVEs for monitoring and analytics. This provides complete view of the system to administrator. Alarms get created/reset when alarm-rules are satisfied. UVEs are added/updated and removed as internal states change. These objects display current state of the system.

# 2. Problem statement
If an alarm gets created and then it is reset after mitigation steps, the system is back to original good state. But the history of alarm triggers/reset over time is lost. Same issue with UVEs.

# 3. Proposed solution
During alarm and UVE processing, increment set/reset or add/update/remove counters and save it in database. Later this database can be queried using contrail-status script.

## 3.1 Alternatives considered
None

## 3.2 API schema changes
2 new structures have been added to display alarm and UVE stats.
```
struct AlarmgenUVEStats {
    1: u32                      add_count
    2: u32                      change_count
    3: u32                      remove_count
}
struct AlarmgenAlarmStats {
    1: u32                      set_count
    2: u32                      reset_count
}
objectlog sandesh AlarmgenUpdate {
     /** @display_name:Alarmgen UVE Stats */
    7: map<string,AlarmgenUVEStats>    uve_stats   (tags="partition,table,.__key")
    /** @display_name:Alarmgen Alarm Stats */
    8: map<string,AlarmgenAlarmStats>  alarm_stats (tags="partition,table,.__key")
}
```

## 3.3 User workflow impact
#### Describe how users will use the feature.

## 3.4 UI changes
Monitor >> Alarms >> Dashboard
On current dashboard, we have an alarms page. This displays currently active alarms. In addition, new page would display alarms history with following fields – alarms set/reset.
UVE history display for UI is not planned for this release, but it will be available via contrail-stats script.


## 3.5 Notification impact
None

# 4. Implementation
## 4.1 Analytics
Alarmgen process in analytics package collects set/reset (for alarms) and add/update/remove (for UVE) counters. These are pushed to database for storage. The counters are collected with following dimensions.
- alarm-stats[partition][table][alarm-name]
- uve-stats[partition][table][uve-type]
So contrail-stats queries can be done per table (node-type) and uve/alarm-type.

## 4.2 UI
Add support for alarms history display. 


# 5. Performance and scaling impact
## 5.1 API and control plane
None

## 5.2 Forwarding performance
None

# 6. Upgrade
None

# 7. Deprecations
struct UVETableCount is removed.

# 8. Dependencies
None

# 9. Testing
## 9.1 Unit tests
```
contrail-stats --table AlarmgenUpdate.uve_stats
               --select Source uve_stats.__key table T=30 "SUM(uve_stats.add_count)" "SUM(uve_stats.change_count)" "SUM(uve_stats.remove_count)"
               --where "uve_stats.__key=NodeStatus"
contrail-stats --table AlarmgenUpdate.uve_stats
               --select Source uve_stats.__key table uve_stats.add_count uve_stats.change_count uve_stats.remove_count
               --where "uve_stats.__key=NodeStatus"
contrail-stats --table AlarmgenUpdate.alarm_stats
               --select Source T=30 alarm_stats.__key table "SUM(alarm_stats.set_count)" "SUM(alarm_stats.reset_count)"
               --where "alarm_stats.__key=process-connectivity"
contrail-stats --table AlarmgenUpdate.alarm_stats
               --select Source alarm_stats.__key table alarm_stats.set_count alarm_stats.reset_count
               --where "alarm_stats.__key=process-connectivity"
```
## 9.2 Dev tests
## 9.3 System tests

# 10. Documentation Impact

# 11. References
