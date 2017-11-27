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

The transfer of data from/to cassandra database is very expensive. Using SASI index, lot of "filtering" of rows would be done done internally in cassandra database.

# 3. Proposed solution
Desired features
- Write an entry to single table
- Index-able on multiple fields

- Neither number of rows nor number of columns should not be very high.
  Having a middle ground for these numbers gives better performance.
- Avoid hotspots in the table.

We achieve the above as follows
- Create message_timestamp table with TS as PRIMARY key.
- Keep only 2 columns as clustering columns. Cassandra sorts the entries on these columns while writing.
  column3 and beyond are non-clustering columns.
- Create SASI keys for the fields - Source, Messagetype, ModuleId.
- Use "partition" as key2 to avoid hotspot - existing feature for current messagetable.
- Following non-primary and non-clustering columns are prepended with "T2:" to avoid very wide rows.
  T2 is prefixed with ":" as delimiter.
  We do indexing on these columns using SASI index.
  SASI index is specified if prefix search support is required. So it is required for all the columns below except Messagetype and ModuleId.
  But cql query doesnt allow mix of SASI and non-SASI indexes hence all the below columns are SASI indexes.
  - Source
  - Messagetype
  - ModuleId
  - object1
  - object2
  - object3
  - object4
  - object5
  - object6
  Instead of having a single very wide row for one Source in the Source secondary index, we would have (TTL in seconds/8) = (48hrs * 3600)/8 = 21600 rows


```
CREATE TABLE "ContrailAnalyticsCql".messagetablev2 (
    key int,        // T2
    key2 int,       // Partition
    column1 int,    // T1   (clustering-col) <<< 23 bits LSB
    column2 uuid,   // UUID (clustering-col)
    column3 text,   // T2:Source
    column4 text,   // T2:Messagetype
    column5 text,   // T2:ModuleId
    column6 text,   // T2:<object-type1>:<object-value1>)
    column7 text,   // T2:<object-type2>:<object-value2>)
    column8 text,   // T2:<object-type3>:<object-value3>)
    column9 text,   // T2:<object-type4>:<object-value4>)
    column10 text,  // T2:<object-type5>:<object-value5>)
    column11 text,  // T2:<object-type6>:<object-value6>)
    column12 ipaddr,// IpAddress
    column13 int,   // Pid
    column14 text,  // Category
    column15 int,   // Level
    column16 text,  // NodeType
    column17 text,  // InstanceId
    column18 int,   // SequenceNum
    column19 int,   // Type
    value   text    // XMLmessage
    PRIMARY KEY ((key, key2), column1, column2)
)

```

We have deprecated the MessageTable which has PRIMARY index key=UUID.
ObjectTable is indexed on T2 and <object-type> and it has UUID column. This UUID is used for second lookup in MessageTable.
In new proposal, MessageTablev2 is not indexed on UUID.
So we plan the following
1. Donot use Objecttable and save these messages in MessageTablev2.
2. Add 2 columns to MessageTablev2, column6(<object-type1>:<object-value1>) and column7(<object-type2>:<object-value2>)
Those values are in key3 and column1 in below query output.

cqlsh:ContrailAnalyticsCql> select * from objecttable WHERE key=177442050 and key2=0 and key3='ObjectGeneratorInfo'
                                                                  (T2)      (partition)
key        | key2 | key3                | column1                                      | column2 | value
-----------+------+---------------------+----------------------------------------------+---------+--------------------------------------
 177442050 |    0 | ObjectGeneratorInfo |                 a1s42:Control:contrail-dns:0 |  235040 | 5e0c5621-7039-4497-a11a-18457a153d72
 177442050 |    0 | ObjectGeneratorInfo |   a1s42:Database:contrail-database-nodemgr:0 |  234939 | 126fa02c-2ad2-4a30-a4cf-b681419ff56d

We would save “ObjectGeneratorInfo:a1s42:Database:contrail-database-nodemgr:0”.
Note the first “:” from left, this will be the delimiter.
3. Query would look like any of the examples below
cqlsh:ContrailAnalyticsCql> select * from MessageTablev2 WHERE key=177442050 and key2=0 and column6='ObjectGeneratorInfo*' ;
cqlsh:ContrailAnalyticsCql> select * from MessageTablev2 WHERE key=177442050 and key2=0 and column6='ObjectGeneratorInfo:a1s42*' ;
cqlsh:ContrailAnalyticsCql> select * from MessageTablev2 WHERE key=177442050 and key2=0 and column6='ObjectGeneratorInfo:a1s42:Database*' ;
Note the "*" in end of match term.
4. Once data is read from db, it would be of the format <object-typeX>:<object-valueX>. We need to remove “<object-typeX>:” in QE before sending it to UI.
5. These columns 6 and 7 could be empty for various messages in MessageTablev2.

We need to decide on handling existing customer data with old schema. We have 3 options
a) Dont do anything. After upgrade customer will lose access to old data.
   New tables names are different from old one, so the old tables would lie dormant till TTL kicks in.
b) migrate old data to new schema
   There are few issues here.
   - If existing database is huge this might take enormous time. All data might not available just after upgrade.
   - Migration might take longer than TTL of data elements.
c) handle old and new tables simultaneously in query-engine
   - Query logic is different for old and new schema tables. So we cannot merge the handling into single code. 
   - New schema logic will be in separate files/functions. At the very beginning of query logic, we decide on which path would be executed.
   - We shall save the timestamp of upgrade. All queries with timestamps before this would query the old tables and the rest would query the new schema tables.
   - Query cannot span across the timestamp when upgrade was done. If it is present in the query, we would return results for the time duration after upgrade.
Conclusion: We shall support option (a).

## 3.1 Alternatives considered
None

## 3.2 API schema changes

## 3.3 User workflow impact
New message-table schema

```
{
    { name : MessageTS,   datatype : int,    index: false },
    { name : Source,      datatype : string, index: true  },
    { name : ModuleId,    datatype : string, index: true  },
    { name : Messagetype, datatype : string, index: true  },
    { name : Category,    datatype : string, index: false },
    { name : IPAddress,   datatype : ipaddr, index: false },
    { name : Pid,         datatype : int,    index: false },
    { name : Level,       datatype : int,    index: false },
    { name : Type,        datatype : int,    index: false },
    { name : InstanceId,  datatype : string, index: false },
    { name : NodeType,    datatype : string, index: false },
    { name : SequenceNum, datatype : int,    index: false },
    { name : Xmlmessage,  datatype : string, index: false }
}
columns added - IPAddress, Pid
columns removed - Context, Keyword
columns changed - Category, Level
                  Earlier they were indexed, now they are non-indexed columns.
```

## 3.4 UI changes


## 3.5 Notification impact
None

# 4. Implementation
## 4.1 Analytics
Query with WHERE option is supported for primary and SASI index. They are also supported for other columns but it wont be efficient.
We publish the primary and SASI indices so that user can make informed decisions during query.


COLLECTOR
- Add support for SASI index in the schema
- Prepend "T2:" to <column{3-11}> while writing to cassandra.

QUERY ENGINE
- Add support for secondary index.
- Add support for passing multiple indices in cql query.
- Prepend "T2:" to <column{3-11> while quering from cassandra.
- Remove "T2:" prefix from <column{3-11}> after reading from cassandra.
- Query will walk through all T2 timestamps range and key2 (Partition range 1-8).

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
