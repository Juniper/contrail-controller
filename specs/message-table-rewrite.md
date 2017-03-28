# 1. Introduction
In current Contrail solution, all messages from various modules to analytics collector are saved in cassandra database in the table "messagetable".
Each message comes with a unique UUID and it is used as primary index for messages in the table.
We have many columns in the table - Source, ModuleId, Messagetype, XMLMessage, etc.
User can query the database with various combinations of arguments for "SELECT" and "WHERE" parameters.

# 2. Problem statement
In current schema for messagetable, we have fixed number of columns.
Number of columns and column-name is selected upfront during creation of the table.
Each message comes with unique UUID so each message is a new row in the table.
We have separate index tables which are indexed by Source, ModuleId, Messagetype, etc.

We have 2 issues here
Issue#1
Everytime we get a message, we need to write it to main table (messagetable) and each index table.
- messagetable indexed on UUID
  All columns and xmlmessage is saved.
- messagetable{source|moduleid|messagetype..} indexed on the column-name.
  In this messagetable{source|moduleid|messagetype..} we save UUID so as to use it as key to get row entry in messagetable.
In essence, there are n+1 writes for an entry to cassandra database where n is the number of index tables.

Issue#2
For a query based on, say Source, we need to do 2 lookups
- lookup in messagetablesource and get UUID
- lookup in messagetable with the UUID to get xml-message content.
In essence, there are 2 reads from cassandra database for an entry.

The transfer of data from/to cassandra database is very expensive. Using secondary index, lot of "filtering" of rows would be done done internally in cassandra database.

# 3. Proposed solution
Desired features
- Write an entry to single table
- Index-able on multiple fields
- Neither number of rows nor number of columns should not be very high.
  Having a middle ground for these numbers gives better performance.
- Avoid hotspots in the table.

We achieve the above as follows
- Create message_timestamp table with TS as PRIMARY key.
- Keep only 2 columns as clustering columns. Cassandra avoids sorts the entries on these columns while writing.
  column3 and beyond are non-clustering columns.
- Create SECONDARY keys for the fields - Source, Messagetype, ModuleId.
- Use "partition" as key2 to avoid hotspot - existing feature for current messagetable.
- Prepend Source, Messagetype, ModuleId, object1 and object2 column values with "T2:" to avoid very wide rows.
  T2 is prefixed in hex with ":" as delimiter.


```
CREATE TABLE "ContrailAnalyticsCql".messagetablev2 (
    key int,        // T2
    key2 int,       // Partition
    column1 int,    // T1               <<< 23 bits LSB
    column2 uuid,   // UUID
    column3 text,   // T2:Source
    column4 text,   // T2:Messagetype
    column5 text,   // T2:ModuleId
    column6 text,   // T2:<object-type1>:<object-value1>)
    column7 text,   // T2:<object-type2>:<object-value2>)
    column8 text,   // Category
    column9 int,    // Level
    column10 int,   // Type
    column11 text,  // InstanceId
    column12 text,  // NodeType
    column13 int,   // SequenceNum
    column14 text,  // Context
    value   text    // XMLmessage
    PRIMARY KEY ((key, key2), column1, column2)
)


CREATE INDEX messagetable_source_index   ON "ContrailAnalyticsCql".messagetablev2 ("column3");
CREATE INDEX messagetable_msgtype_index  ON "ContrailAnalyticsCql".messagetablev2 ("column4");
CREATE INDEX messagetable_moduleid_index ON "ContrailAnalyticsCql".messagetablev2 ("column5");
CREATE INDEX messagetable_object1_index  ON "ContrailAnalyticsCql".messagetablev2 ("column6");
CREATE INDEX messagetable_object2_index  ON "ContrailAnalyticsCql".messagetablev2 ("column7");
```

We have deprecated the MessageTable which has PRIMARY index key=UUID.
ObjectTable is indexed on T2 and <object-type> and it has UUID column. This UUID is used for second lookup in MessageTable.
In new proposal, MessageTablev2 is not indexed on UUID.
So we plan the following
1. Remove object table and save these messages in MessageTablev2.
2. Add 2 columns to MessageTablev2, column6(<object-type1>:<object-value1>) and column7(<object-type2>:<object-value2>)
Those values are in key3 and column1 in below query output.

cqlsh:ContrailAnalyticsCql> select * from objecttable WHERE key=177442050 and key2=0 and key3='ObjectGeneratorInfo'
key        | key2 | key3                | column1                                      | column2 | value
-----------+------+---------------------+----------------------------------------------+---------+--------------------------------------
 177442050 |    0 | ObjectGeneratorInfo |                 a1s42:Control:contrail-dns:0 |  235040 | 5e0c5621-7039-4497-a11a-18457a153d72
 177442050 |    0 | ObjectGeneratorInfo |   a1s42:Database:contrail-database-nodemgr:0 |  234939 | 126fa02c-2ad2-4a30-a4cf-b681419ff56d

We would save “ObjectGeneratorInfo:a1s42:Database:contrail-database-nodemgr:0”.
Note the first “:” from left, this will be the delimiter.
3. Query would look like
cqlsh:ContrailAnalyticsCql> select * from MessageTablev2 WHERE key=177442050 and key2=0 and column6='ObjectGeneratorInfo*' ;
Note the "*" in 'ObjectGeneratorInfo*'.
4. Once data is read from db, it would be of the format <object-typeX>:<object-valueX>. We need to remove “<object-typeX>:” in QE before sending it to UI.
5. These columns 6 and 7 could be empty for various messages in MessageTablev2.

In conclusion, we will have 2 tables going forward
- MessageTablev2
- StatsTable
- FlowTable

## 3.1 Alternatives considered
None

## 3.2 API schema changes

## 3.3 User workflow impact
#### Describe how users will use the feature.

## 3.4 UI changes


## 3.5 Notification impact
None

# 4. Implementation
## 4.1 Analytics
Query with WHERE option is supported for primary and secondary index. They are also supported for other columns but it wont be efficient.
We publish the primary and secondary indices so that user can make informed decisions during query.


COLLECTOR
- Add support for secondary index
- Prepend "T2:" to <column{3|4|5|6|7}> while writting to cassandra.

QUERY ENGINE
- Add support for secondary index.
- Add support for passing multiple indices in cql query.
- Prepend "T2:" to <column{3|4|5|6|7}> while quering from cassandra.
- Remove "T2:" prefix from <column{3|4|5|6|7}> after reading from cassandra.
- Query will walk through all T2 timestamps range and key2 (Partition range 1-16).

## 4.2 UI
No feature to be added. See section "Deprecations" for other changes needed.

# 5. Performance and scaling impact
## 5.1 API and control plane
None

## 5.2 Forwarding performance
None

# 6. Upgrade
None

# 7. Deprecations
Earlier MessageTable was indexed on the following fields too - Level, Keyword. These index tables will be removed.
Backward compatibility NOT supported for queries on the above fields.

# 8. Dependencies
None

# 9. Testing
## 9.1 Unit tests
```
SELECT * FROM "ContrailAnalyticsCql".messagetablev2 WHERE T2='xxx'     AND column3='a1s42' AND column4='NodeStatusUVE' AND column5='contrail-alarm-gen'
SELECT * FROM "ContrailAnalyticsCql".messagetablev2 WHERE T2='xxx+8s'  AND column3='a1s42' AND column4='NodeStatusUVE' AND column5='contrail-alarm-gen'
SELECT * FROM "ContrailAnalyticsCql".messagetablev2 WHERE T2='xxx+16s' AND column3='a1s42' AND column4='NodeStatusUVE' AND column5='contrail-alarm-gen'

SELECT * FROM "ContrailAnalyticsCql".messagetablev2 WHERE T2='xxx'     AND column3='a1s42' AND column4='NodeStatusUVE' AND column5='contrail-alarm-gen'
                                                                                                    OR
                                                                           column3='a6s40' AND column4='UserDefinedLogStatisticUVE'

```

## 9.2 Dev tests
## 9.3 System tests

# 10. Documentation Impact

# 11. References
