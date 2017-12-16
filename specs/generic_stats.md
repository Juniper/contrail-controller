# 1. Introduction
Generic stats provide the function of key data statistics in system log, which
permit customer customize key data.

# 2. Problem statement
Currently, system log can carry important customer application info, but
usually the info formats are different which depend on different applications.
Even for the same application, different formats info can be carried. We need
provide a way to collect, extract and do statistics for key data in system log.

# 3. Proposed solution
Basic requirement:
From configuration, permit user config name pattern, key list, tag list and candidate metric
list.

  - Name is the id of the item configured by customer.
  - Pattern is used to match syslog body, identify and extract syslog body info.
  - key list is used to generate one table for stats
  - candidate metric list is the object to be counted.

According to user configuration, syslog listener will do filter to all received
syslog, if match an UVE will generated.

For example:

```
 <34>1 2009-12-12T12:12:51.403Z - haproxy 14387 - 10.0.1.2:33313 [06/Feb/2009:12:12:51.443] fnt bck/srv1 0/0/5007 212 -- 0/0/0/0/3 0/0
```

This is a syslog body for haproxy TCP. To collect and do stats for bytes read,
customer can configure:

  - name: HAPROXYTCP
  - pattern:

```
%{IP:client_ip}:%{INT:client_port} \[%{HAPROXYDATE:accept_date}\] %{NOTSPACE:frontend_name} %{NOTSPACE:backend_name}/%{NOTSPACE:server_name} %{INT:time_queue}/%{INT:time_backend_connect}/%{NOTSPACE:time_duration} %{NOTSPACE:bytes_read} %{NOTSPACE:termination_state} %{INT:actconn}/%{INT:feconn}/%{INT:beconn}/%{INT:srvconn}/%{NOTSPACE:retries} %{INT:srv_queue}/%{INT:backend_queue}
```
  - key list: client_ip, client_port
  - candidate metric list: bytes_read

When a syslog body match this pattern, an UVE with client_ip, client_port
as keys, bytes_read value as metric will be generated. If metric list has
multi items, multi UVEs will be generated, one UVE for one candidate metric.

Standard consistency and performance:
Besides above basic configuration, we need consider standard consistency
and performance.

The latest syslog standard is RFC5424, according to this document, syslog
consists of three parts:

  - HEADER
  - STRUCTURED-DATA (0, 1 or mutli instances)
  - MSG (0 or 1 instance)

The pattern configured by customer is used to match one STRUCTURED-DATA or
MSG.

At same time, we need parse syslog header since important info is carried.
One scenario in future is that customer configure lots of patterns for lots
of application. Since syslog match is sequential processing, we need consider
separate customer configuration to different domain. The key word is used
to identify domain just comes from header.
The header of syslog (RFC5424) involves:

  - PRI
  - VERSION
  - TIMESTAMP
  - HOSTNAME
  - APP-NAME
  - PROCID
  - MSGID

User can select one or more of HOSTNAME, APP-NAME, PROCID and MSGID as keys.

We select APP-NAME and MSGID as domain key. User can select one or both of
them as classifier, if configure nothing, only one domain is generated, and
performance will be impacted.

## 3.1 Alternatives considered
None
## 3.2 API schema changes

A new configure xsd --- usr_def_syslog_parser.xsd is added.

```
<!--
Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
     -->
<xsd:schema xmlns:xsd="http://www.w3.org/2001/XMLSchema">

<xsd:complexType name="UserDefinedSysLogParser">
    <xsd:element name="name" type="xsd:string" required="true"
                                        description="syslog parser name"/>
    <xsd:element name="hostname" type="xsd:string" default=""/>
    <xsd:element name="hostname_as_tag" type="xsd:boolean" default="false"/>
    <xsd:element name="appname" type="xsd:string" default=""/>
    <xsd:element name="appname_as_tag" type="xsd:boolean" default="false"/>
    <xsd:element name="appname_as_classfier" type="xsd:boolean" default="false"/>
    <xsd:element name="procid" type="xsd:boolean" default=""/>
    <xsd:element name="procid_as_tag" type="xsd:boolean" default="false"/>
    <xsd:element name="msgid" type="xsd:boolean" default=""/>
    <xsd:element name="msgid_as_tag" type="xsd:boolean" default="false"/>
    <xsd:element name="msgid_as_classfier" type="xsd:boolean" default="true"/>
    <xsd:element name="pattern" type="xsd:string" required="true"/>
    <xsd:element name="tag_list"  type="xsd:string" required="true"/>
    <xsd:element name="metric_list" type="xsd:string" required="true"/>
</xsd:complexType>

<xsd:complexType name="UserDefinedSysLogParserList">
    <xsd:element name="parserlist" type="UserDefinedSysLogParser" require="true"
                                                           maxOccurs="unbounded"/>
</xsd:complexType>

<xsd:element name="user-defined-syslog-parsers" type="UserDefinedSysLogParserList"
    description_1='User will create some user-defined-syslog-parser,'
    description_2='identified by name a typical expression is like:'
    description_3='%{PATTERN_NAME:capture_name:data_type}'
    description_4='keylist and metriclist must be one or more capture_name in pattern'
    description_5='and metriclist member data_type must be int'
    description_6='Any match system log will generate uve with keylist and metriclist'/>
<!--#IFMAP-SEMANTICS-IDL
    MapProperty('user-defined-syslog-parsers', 'global-system-config', 'name',
           'optional', 'CRUD') -->
</xsd:schema>
```

This configuration will be one part of global confguration:

```
diff --git a/src/schema/vnc_cfg.xsd b/src/schema/vnc_cfg.xsd
index b3d73de..ce20e77 100644
--- a/src/schema/vnc_cfg.xsd
+++ b/src/schema/vnc_cfg.xsd
@@ -3021,6 +3021,9 @@ targetNamespace="http://www.contrailsystems.com/2012/VNC-CONFIG/0">
 <!-- Userdefined Counter Config -->
 <xsd:include schemaLocation='usr_def_cntr.xsd'/>

+<!-- Userdefined syslog pattern Config -->
+<xsd:include schemaLocation='usr_def_syslog_parser.xsd'/>
+
 <!-- Firewall policy -->
 <xsd:include schemaLocation='firewall_policy.xsd'/>
```

## 3.3 User workflow impact
None

## 3.4 UI changes
UI shall provide a new configuration list for this feature according to xsd.

## 3.5 Notification impact
- Call stat_walk api directly:

```
      StatWalker(StatTableInsertFn fn, const uint64_t &timestamp,
                const std::string& statName, const TagMap& tags)
```
      fn = DbHandler::StatTableInsert
      timestamp = timestamp received syslog
      statName  = pattern name of user configured
      tags      = pair<Source:source ip>
                  pair<name: join key list captured value with separator configured by customer>

```
      void
      StatWalker::Push(const std::string& name,
          const TagMap& tags,
          const DbHandler::AttribMap& attribs)
```

      name = one of metric list
      tags = NULL
      attribs = pair<one of metric list, value>

We use the following example to explain how to fill UVE:
user configuration:

  -  appname: haproxy appname_as_classfier:yes appname_as_uve_key:no hostname_as_uve_tag:no
  -  msgid: ""        appname_as_classfier:no  msgid_as_uve_key:no msgid_as_uve_tag:no
  -  name: HAPROXYTCP
  -  pattern:

```
%{IP:client_ip}:%{INT:client_port} \[%{HAPROXYDATE:accept_date}\] %{NOTSPACE:frontend_name} %{NOTSPACE:backend_name}/%{NOTSPACE:server_name} %{INT:time_queue}/%{INT:time_backend_connect}/%{NOTSPACE:time_duration} %{NOTSPACE:bytes_read} %{NOTSPACE:termination_state} %{INT:actconn}/%{INT:feconn}/%{INT:beconn}/%{INT:srvconn}/%{NOTSPACE:retries} %{INT:srv_queue}/%{INT:backend_queue}
```

  - key list: client_ip, client_port
  - user_seperator: "::"
  - candidate metric list: bytes_read, retries

 syslog:

```
 <34>1 2009-12-12T12:12:51.403Z - haproxy 14387 - \ 10.0.1.2:33313 [06/Feb/2009:12:12:51.443] fnt bck/srv1 0/0/5007 212 -- 0/0/0/0/3 0/0
```
  From syslog, we can get appname (haproxy), client_ip (10.0.1.2), client_port (33313), frontend_name (fnt) and bytes (212), retries(3) so:

```
    Toptag = pair<Source, 10.0.1.2>, pair<name, 10.0.1.2::33313>
    StatWalker(DbHandler::StatTableInsert, timestamp, “HAPROXYTCP”, Toptag)

    Tags = NULL
    Attribs = pair<bytes_read, 212>
    StatWalker::Push(“bytes_read”, Tags, Attribs)
    StatWalker::Pop()

```

```
    Toptag = pair<Source, 10.0.1.2>, pair<name, 10.0.1.2::3313>
    StatWalker(DbHandler::StatTableInsert, timestamp, “HAPROXYTCP”, Toptag)

    Tags = NULL
    Attribs = pair<retries, 3>
    StatWalker::Push(“retries”, Tags, Attribs)
    StatWalker::Pop()

```

- Add new introspect api to verify configuration:

```

struct LogStatisticConfigInfo {
    1: string name;
    2: string pattern;
}

request sandesh LogStatisticConfigInfoGetRequest {
}

response sandesh LogStatisticConfigInfoResponse {
    1: list<LogStatisticConfigInfo> config_info;
}

struct LogPatternConfigInfo {
    1: string name;
    2: string pattern;
    3: list<string> keys;
    4: list<string> metrics;
}

struct LogParserConfigInfo {
    1: string msgid_appname;
    2: list<LogPatternConfigInfo> pattern_infos;
}

request sandesh LogParserConfigInfoGetRequest {
}

response sandesh LogParserConfigInfoResponse {
    1: list<LogParserConfigInfo> config_info;
}
```

# 4. Implementation
## 4.1 overview:

```
     ---------------         -------------------------
    | viz_collector |       | config_client_collector |
     --------------          ------------------------

     ---------------------------
    | user_define_syslog_parser |
     --------------------------

     -------------       -----------------   ---------------
    | grok_parser |     | syslog_listener | | syslog_parser |
     ------------        ----------------    ---------------

```

A new class user_define_syslog_parser will be created by viz_collector. It
will register callback to config_client_collector to receive configuration,
call create/delete/match API of grok parser. At same time, register callback
to syslog listener, and call syslog_parser API to parser syslog header.

## 4.2 data structure:

- Domain list:
  Domain list is used to store map between domain key and pattern list.
  Since pattern can be identified by name, so a string to set<string>
  map is enough.
  To key string, it should like "appname:msgid", when any one is absent of
  appname and msgid, '-' is used to replace.

```
typedef std::map<std::string, std::set<std::string> > DomainList
```

- pattern map:
  Grok parser mould has legacy name <--> pattern map internal, we will
  reuse it and do not create external map.

- attribute (keys/metrics) map:
  This map use name as key.

```
struct UserDefKeyMetric {
    UserDefKeyMetric() {}
    UserDefKeyMetric(std::vector<std::string> &keys,
                     std::vector<std::string> &metrics) {
        keys_ = keys;
        metrics_ = metrics;
    }
    std::vector<std::string> keys_;
    std::vector<std::string> metrics_;
};
typedef std::map<std::string, UserDefKeyMetric> KeyMetricMap;
```

## 4.3 system log process flow:

  1. when receive a system log, use syslog_parser to get header info.
  2. Check "appname:-" "-:msgid" "appname:msgid" in DomainList or not,
       if not, drop it.
  3. Get Structured_Data and MSG of syslog, walk set<string> picked with
       domain key in step 2, according to name to call grok API match.
       Grok API will return false if no match, else a map<string, string>
       will be returned. Check all keys/metrics (from KeyMetricMap with name)
       in map<string, string>. If pass, generate an UVE message.
## 4.4 Modification to legecy code.
   - viz_collector:
     A scoped pointer of user_define_syslog_parser is added to viz_collector.
   - config_client_collector:
     A new callback function is registered to config_client_collector
   - syslog_listener:
     Add new API to permit other module register callback function. Currently
     one instance only register one callback. This lead syslog_listener can not
     be shared. We do not create a new instance for this project, just want to
     share the instance with system log collector module.
   - syslog_parser:
     Support RFC5424.current code support RFC3164, but sreuctured syslog is
     using it, to avoid breaking legacy feature, a new parse function
     syslog_rfc_parser will be added.
   - grok_parser:
     Remove configuration UVE generate code, it will be done by user_define_syslog_parser
     Add create/delete pattern API.
     Change match API to do match with given name, but not walk all grok instance.

# 5. Performance and scaling impact
When lots of pattern configured in one domain, performence will be low.

# 6. Upgrade
Config node do configuration consistency when upgrading.

# 7. Deprecations
None

# 8. Dependencies
None

# 9. Testing
## 9.1 Unit tests

## 9.2 Dev tests

## 9.3 System tests

# 10. Documentation Impact

# 11. References
1. [logstash/patterns/haproxy](https://github.com/elastic/logstash/blob/v1.4.2/patterns/haproxy)
2. [HAProxy Configuration Manual](https://www.haproxy.org/download/1.8/doc/configuration.txt)
3. [The Syslog Protocol](https://tools.ietf.org/rfc/rfc5424.txt)
