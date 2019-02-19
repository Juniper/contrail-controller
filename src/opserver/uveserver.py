#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

#
# UVEServer
#
# Operational State Server for UVEs
#

import gevent
import json
import copy
import xmltodict
import redis
import datetime
import sys
from opserver_util import OpServerUtils
import re
from gevent.lock import BoundedSemaphore
from pysandesh.util import UTCTimestampUsec
from pysandesh.connection_info import ConnectionState
from sandesh.viz.constants import UVE_MAP
from pysandesh.gen_py.process_info.ttypes import ConnectionType,\
     ConnectionStatus
import traceback

class UVEServer(object):

    def __init__(self, redis_uve_server, logger, redis_password=None, \
            uvedbcache=None, usecache=False):
        self._local_redis_uve = redis_uve_server
        self._redis_uve_map = {}
        self._logger = logger
        self._redis = None
        self._uvedbcache = uvedbcache
        self._usecache = usecache
        self._redis_password = redis_password
        self._uve_reverse_map = {}
        for h,m in UVE_MAP.iteritems():
            self._uve_reverse_map[m] = h

    #end __init__
    def redis_instances(self):
        return set(self._redis_uve_map.keys())

    def update_redis_uve_list(self, redis_uve_list):
        newlist = set(redis_uve_list)
        chg = False
        # if some redis instances are gone, remove them from our map
        for test_elem in self._redis_uve_map.keys():
            if test_elem not in newlist:
                chg = True
                r_ip = test_elem[0]
                r_port = test_elem[1]
                del self._redis_uve_map[test_elem]
                ConnectionState.delete(ConnectionType.REDIS_UVE,\
                    r_ip+":"+str(r_port)) 
        
        # new redis instances need to be inserted into the map
        for test_elem in newlist:
            if test_elem not in self._redis_uve_map:
                chg = True
                r_ip = test_elem[0]
                r_port = test_elem[1]
                self._redis_uve_map[test_elem] = None
                ConnectionState.update(ConnectionType.REDIS_UVE,\
                    r_ip+":"+str(r_port), ConnectionStatus.INIT,
                    [r_ip + ":" + str(r_port)],
                    message = 'Insert New Redis Instance')
        if chg:
            self._logger.error("Updated redis_uve_list %s" % 
                                str(self._redis_uve_map))

        # Exercise redis connections to update health
        if len(newlist):
            self.get_uve("ObjectCollectorInfo:__NONE__", False, None)

    # end update_redis_uve_list

    def fill_redis_uve_info(self, redis_uve_info):
        redis_uve_info.ip = self._local_redis_uve[0]
        redis_uve_info.port = self._local_redis_uve[1]
        redish =  redis.StrictRedis(self._local_redis_uve[0],
                                    self._local_redis_uve[1],
                                    password=self._redis_password,
                                    db=1)
        try:
            redish.ping()
        except redis.exceptions.ConnectionError:
            redis_uve_info.status = 'DisConnected'
        else:
            redis_uve_info.status = 'Connected'
    #end fill_redis_uve_info

    def run(self):
	ConnectionState.update(conn_type = ConnectionType.REDIS_UVE,
            name = 'LOCAL', status = ConnectionStatus.INIT,
            message = 'Local Redis Instance initialized',
            server_addrs = ['%s:%s' % 
                (self._local_redis_uve[0],self._local_redis_uve[1])])
        while True:
            if self._redis:
                redish = self._redis
            else: 

                redish =  redis.StrictRedis(self._local_redis_uve[0],
                                            self._local_redis_uve[1],
                                            password=self._redis_password,
                                            db=1)
            try:
                if not self._redis:
                    value = ""
                    redish.ping()
                else:
                    k, value = redish.brpop("DELETED")
                    self._logger.debug("%s del received for " % value)
                    # value is of the format: 
                    # DEL:<key>:<src>:<node-type>:<module>:<instance-id>:<message-type>:<seqno>
                    redish.delete(value)
            except gevent.GreenletExit:
                self._logger.error('UVEServer Exiting on gevent-kill')
                break
            except:
                if self._redis:
                    #send redis connection down msg. Coule be bcos of authentication
                    ConnectionState.update(conn_type = ConnectionType.REDIS_UVE,
                        name = 'LOCAL', status = ConnectionStatus.DOWN,
                        message = 'Local Redis Instance is down',
                        server_addrs = ['%s:%s' % 
                            (self._local_redis_uve[0],
                            self._local_redis_uve[1])])
                    self._redis = None
                gevent.sleep(5)
            else:
                self._logger.debug("Deleted %s" % value)
                if not self._redis:
                    self._redis = redish
                    ConnectionState.update(conn_type = ConnectionType.REDIS_UVE,
                        name = 'LOCAL', status = ConnectionStatus.UP,
                        message = 'Local Redis Instance updated',
                        server_addrs = ['%s:%s' % 
                            (self._local_redis_uve[0],
                            self._local_redis_uve[1])])

    @staticmethod
    def _is_agg_list(attr):
        if attr['@type'] in ['list']:
            if '@aggtype' in attr:
                if attr['@aggtype'] == "append":
                    return True
        return False

    def get_part(self, part, r_inst):
        # Get UVE and Type contents of given partition on given
        # collector/redis instance.
        uves = {}
        try:
            r_ip = r_inst[0]
            r_port = r_inst[1]
            redish = self._redis_inst_get(r_inst)
            gen_uves = {}
            for elems in redish.smembers("PART2KEY:" + str(part)): 
                info = elems.split(":", 5)
                gen = info[0] + ":" + info[1] + ":" + info[2] + ":" + info[3]
                typ = info[4]
                key = info[5]
                if not gen in gen_uves:
                     gen_uves[gen] = {}
                if not key in gen_uves[gen]:
                     gen_uves[gen][key] = {}
                gen_uves[gen][key][typ] = {}
        except Exception as e:
            self._logger.error("get_part failed %s for : %s:%d tb %s" \
                               % (str(e), r_ip, r_port, traceback.format_exc()))
            self._redis_inst_down(r_inst)
        else:
            self._redis_inst_up(r_inst, redish)
        return r_ip + ":" + str(r_port) , gen_uves

    def _redis_inst_get(self, r_inst):
        r_ip = r_inst[0]
        r_port = r_inst[1]
        if r_inst in self._redis_uve_map and not self._redis_uve_map[r_inst]:
            return redis.StrictRedis(
                        host=r_ip, port=r_port,
                        password=self._redis_password, db=1, socket_timeout=30)
        else:
            if r_inst in self._redis_uve_map:
                return self._redis_uve_map[r_inst]
            else:
                self._logger.error("redis instance %s not in _redis_uve_map" %
                    (str(r_inst)))
                return None

    def _redis_inst_up(self, r_inst, redish):
	if r_inst in self._redis_uve_map and not self._redis_uve_map[r_inst]:
            r_ip = r_inst[0]
            r_port = r_inst[1]
	    self._redis_uve_map[r_inst] = redish
	    ConnectionState.update(ConnectionType.REDIS_UVE,\
		r_ip + ":" + str(r_port), ConnectionStatus.UP,
                [r_ip + ":" + str(r_port)], message = 'Redis Instance is up')

    def _redis_inst_down(self, r_inst):
	if r_inst in self._redis_uve_map and self._redis_uve_map[r_inst]:
            r_ip = r_inst[0]
            r_port = r_inst[1]
	    self._redis_uve_map[r_inst] = None
	    ConnectionState.update(ConnectionType.REDIS_UVE,
		    r_ip + ":" + str(r_port), ConnectionStatus.DOWN, 
                    message = 'Redis Instance is down')
 
    def get_tables(self):
        tables = set() 
        for r_inst in self._redis_uve_map.keys():
            try:
                redish = self._redis_inst_get(r_inst)
                tbs = [elem.split(":",1)[1] for elem in redish.keys("TABLE:*")]
                tables.update(set(tbs))
            except Exception as e:
                self._logger.error("get_tables failed %s for : %s tb %s" \
                               % (str(e), str(r_inst), traceback.format_exc()))
                self._redis_inst_down(r_inst)
            else:
                self._redis_inst_up(r_inst, redish)

        return tables

    def get_uve(self, key, flat, filters=None, base_url=None):

        filters = filters or {}
        sfilter = filters.get('sfilt')
        mfilter = filters.get('mfilt')
        tfilter = filters.get('cfilt')
        ackfilter = filters.get('ackfilt')

        if flat and not sfilter and not mfilter and self._usecache:
            return self._uvedbcache.get_uve(key, filters)

        is_alarm = False
        if tfilter == "UVEAlarms": 
            is_alarm = True

        state = {}
        state[key] = {}
        rsp = {}
        failures = False

        tab = key.split(":",1)[0]
 
        for r_inst in self._redis_uve_map.keys():
            try:
                redish = self._redis_inst_get(r_inst)
                qmap = {}

                ppe = redish.pipeline()
                ppe.smembers("ALARM_ORIGINS:" + key)
                if not is_alarm:
                    ppe.smembers("ORIGINS:" + key)
                pperes = ppe.execute()
                origins = set()
                for origset in pperes:
                    for smt in origset:
                        tt = smt.rsplit(":",1)[1]
                        sm = smt.rsplit(":",1)[0]
                        source = sm.split(":", 1)[0]
                        mdule = sm.split(":", 1)[1]
                        if tfilter is not None:
                            if tt not in tfilter:
                                continue
                        if sfilter is not None:
                            if sfilter != source:
                                continue
                        if mfilter is not None:
                            if mfilter != mdule:
                                continue
                        origins.add(smt)

                ppeval = redish.pipeline()
                for origs in origins:
                    ppeval.hgetall("VALUES:" + key + ":" + origs)
                odictlist = ppeval.execute()

                idx = 0    
                for origs in origins:

                    odict = odictlist[idx]
                    idx = idx + 1

                    info = origs.rsplit(":", 1)
                    dsource = info[0]
                    typ = info[1]

                    afilter_list = set()
                    if tfilter is not None:
                        afilter_list = tfilter[typ]

                    for attr, value in odict.iteritems():
                        if len(afilter_list):
                            if attr not in afilter_list:
                                continue

                        if value[0] == '<':
                            try:
                                snhdict = xmltodict.parse(value)
                            except:
                                self._logger.error("xml parsing failed key %s, struct %s: %s" \
                                    % (key, typ, str(value)))
                                continue

                            if snhdict[attr]['@type'] == 'list':
                                sname = ParallelAggregator.get_list_name(
                                        snhdict[attr])
                                if snhdict[attr]['list']['@size'] == '0':
                                    continue
                                elif snhdict[attr]['list']['@size'] == '1':
                                    if not isinstance(
                                        snhdict[attr]['list'][sname], list):
                                        snhdict[attr]['list'][sname] = [
                                            snhdict[attr]['list'][sname]]
                                if typ == 'UVEAlarms' and attr == 'alarms' and \
                                        ackfilter is not None:
                                    alarms = []
                                    for alarm in snhdict[attr]['list'][sname]:
                                        ack_attr = alarm.get('ack')
                                        if ack_attr:
                                            ack = ack_attr['#text']
                                        else:
                                            ack = 'false'
                                        if ack == ackfilter:
                                            alarms.append(alarm)
                                    if not len(alarms):
                                        continue
                                    snhdict[attr]['list'][sname] = alarms
                                    snhdict[attr]['list']['@size'] = \
                                        str(len(alarms))
                        else:
                            continue

                        # print "Attr %s Value %s" % (attr, snhdict)
                        if typ not in state[key]:
                            state[key][typ] = {}
                        if attr not in state[key][typ]:
                            state[key][typ][attr] = {}
                        if dsource in state[key][typ][attr]:
                            self._logger.debug(\
                            "Found Dup %s:%s:%s:%s:%s = %s" % \
                                (key, typ, attr, source, mdule, state[
                                key][typ][attr][dsource]))
                        state[key][typ][attr][dsource] = snhdict[attr]

                pa = ParallelAggregator(state, self._uve_reverse_map)
                rsp = pa.aggregate(key, flat, base_url)
            except Exception as e:
                self._logger.error("redis-uve failed %s for key %s: %s tb %s" \
                               % (str(e), key, str(r_inst), traceback.format_exc()))
                self._redis_inst_down(r_inst)
                failures = True
            else:
                self._redis_inst_up(r_inst, redish)
                self._logger.debug("Computed %s as %s" % (key,rsp.keys()))

        return failures, rsp
    # end get_uve

    def get_uve_regex(self, key):
        regex = ''
        if key[0] != '*':
            regex += '^'
        regex += key.replace('*', '.*?')
        if key[-1] != '*':
            regex += '$'
        return re.compile(regex)
    # end get_uve_regex

    def get_alarms(self, filters):
        tablesfilt = filters.get('tablefilt')
        kfilter = filters.get('kfilt')
        patterns = None
        if kfilter is not None:
            patterns = set()
            for filt in kfilter:
                patterns.add(self.get_uve_regex(filt))
        if self._usecache:
            rsp = self._uvedbcache.get_uve_list(tablesfilt, filters, patterns, False)
        else:
            tables = self.get_tables()
            rsp = {}
            for table in tables:
                uve_list = {}
                if tablesfilt is not None:
                    if table not in tablesfilt:
                        continue
                uve_keys = self.get_uve_list(table, filters, False)
                for uve_key in uve_keys:
                    _,uve_val = self.get_uve(
                        table + ':' + uve_key, True, filters)
                    if uve_val == {}:
                        continue
                    else:
                        uve_list[uve_key] = uve_val
                if len(uve_list):
                    rsp[table] = uve_list
        return rsp
    # end get_alarms

    def multi_uve_get(self, table, flat, filters=None, base_url=None):
        sfilter = filters.get('sfilt')
        mfilter = filters.get('mfilt')
        kfilter = filters.get('kfilt')

        patterns = None
        if kfilter is not None:
            patterns = set()
            for filt in kfilter:
                patterns.add(self.get_uve_regex(filt))

        if not sfilter and not mfilter and self._usecache:
            rsp = self._uvedbcache.get_uve_list([table], filters, patterns, False)
            if table in rsp:
                for uve_name in rsp[table]:
                    yield {'name': uve_name, 'value': rsp[table][uve_name]}
        else:
            # get_uve_list cannot handle attribute names very efficiently,
            # so we don't pass them here
            uve_list = self.get_uve_list(table, filters, False)

            for uve_name in uve_list:
                _,uve_val = self.get_uve(
                    table + ':' + uve_name, flat, filters,  base_url)
                if uve_val == {}:
                    continue
                else:
                    yield {'name': uve_name, 'value': uve_val}
    # end multi_uve_get

    def get_uve_list(self, table, filters=None, parse_afilter=False):
        is_alarm = False
        filters = filters or {}
        tfilter = filters.get('cfilt')
        if tfilter == "UVEAlarms": 
            is_alarm = True
        uve_list = set()
        kfilter = filters.get('kfilt')
        sfilter = filters.get('sfilt')
        mfilter = filters.get('mfilt')

        patterns = None
        if kfilter is not None:
            patterns = set()
            for filt in kfilter:
                patterns.add(self.get_uve_regex(filt))

        if not sfilter and not mfilter and self._usecache:
            rsp = self._uvedbcache.get_uve_list([table], filters, patterns)
            if table in rsp:
                uve_list = rsp[table]
            return uve_list

        for r_inst in self._redis_uve_map.keys():
            try:
                redish = self._redis_inst_get(r_inst)

                # For UVE queries, we wanna read both UVE and Alarm table
                entries = redish.smembers('ALARM_TABLE:' + table)
                if not is_alarm:
                    entries = entries.union(redish.smembers('TABLE:' + table))
                for entry in entries:
                    info = (entry.split(':', 1)[1]).rsplit(':', 5)
                    uve_key = info[0]
                    if kfilter is not None:
                        kfilter_match = False
                        for pattern in patterns:
                            if pattern.match(uve_key):
                                kfilter_match = True
                                break
                        if not kfilter_match:
                            continue
                    src = info[1]
                    if sfilter is not None:
                        if sfilter != src:
                            continue
                    module = info[2]+':'+info[3]+':'+info[4]
                    if mfilter is not None:
                        if mfilter != module:
                            continue
                    typ = info[5]
                    if tfilter is not None:
                        if typ not in tfilter:
                            continue
                    if parse_afilter:
                        if tfilter is not None and len(tfilter[typ]):
                            valkey = "VALUES:" + table + ":" + uve_key + \
                                ":" + src + ":" + module + ":" + typ
                            for afilter in tfilter[typ]:
                                attrval = redish.hget(valkey, afilter)
                                if attrval is not None:
                                    break
                            if attrval is None:
                                continue
                    uve_list.add(uve_key)
            except Exception as e:
                self._logger.error("get_uve_list failed %s for : %s tb %s" \
                               % (str(e), str(r_inst), traceback.format_exc()))
                self._redis_inst_down(r_inst)
            else:
                self._redis_inst_up(r_inst, redish)
        return uve_list
    # end get_uve_list

    def get_uvedb_cache_tables(self):
        if not self._usecache:
            return []
        return self._uvedbcache.get_uvedb_cache_tables()
    # end get_uvedb_cache_tables

    def get_uvedb_cache_table_keys(self, table):
        if not self._usecache:
            return []
        return self._uvedbcache.get_uvedb_cache_table_keys(table)
    # end get_uvedb_cache_table_keys

    def get_uvedb_cache_uve(self, table, uve_key):
        if not self._usecache:
            return None
        return self._uvedbcache.get_uvedb_cache_uve(table, uve_key)
    # end get_uvedb_cache_uve


# end UVEServer


class ParallelAggregator:

    def __init__(self, state, rev_map = {}):
        self._state = state
        self._rev_map = rev_map

    def _default_agg(self, oattr):
        itemset = set()
        result = []
        for source in oattr.keys():
            elem = oattr[source]
            hdelem = json.dumps(elem)
            if hdelem not in itemset:
                itemset.add(hdelem)
                result.append([elem, source])
            else:
                for items in result:
                    if elem in items:
                        items.append(source)
        return result

    def _is_elem_sum(self, oattr):
        akey = oattr.keys()[0]
        if oattr[akey]['@type'] not in ['i8', 'i16', 'i32', 'i64',
                                    'byte', 'u8', 'u16', 'u32', 'u64']:
            return False
        if '@aggtype' not in oattr[akey]:
            return False
        if oattr[akey]['@aggtype'] != "sum":
            return False
        return True

    def _is_struct_sum(self, oattr):
        akey = oattr.keys()[0]
        if oattr[akey]['@type'] != "struct":
            return False
        if '@aggtype' not in oattr[akey]:
            return False
        if oattr[akey]['@aggtype'] != "sum":
            return False
        return True

    def _is_list_union(self, oattr):
        akey = oattr.keys()[0]
        if not oattr[akey]['@type'] in ["list"]:
            return False
        if '@aggtype' not in oattr[akey]:
            return False
        if oattr[akey]['@aggtype'] in ["union"]:
            return True
        else:
            return False

    def _is_map_union(self, oattr):
        akey = oattr.keys()[0]
        if not oattr[akey]['@type'] in ["map"]:
            return False
        if '@aggtype' not in oattr[akey]:
            return False
        if oattr[akey]['@aggtype'] in ["union"]:
            return True
        else:
            return False

    def _is_append(self, oattr):
        akey = oattr.keys()[0]
        if not oattr[akey]['@type'] in ["list"]:
            return False
        if '@aggtype' not in oattr[akey]:
            return False
        if oattr[akey]['@aggtype'] in ["append"]:
            return True
        else:
            return False

    @staticmethod
    def get_list_name(attr):
        sname = ""
        for sattr in attr['list'].keys():
            if sattr[0] not in ['@']:
                sname = sattr
        return sname

    @staticmethod
    def _get_list_key(elem):
        skey = ""
        for sattr in elem.keys():
            if '@aggtype' in elem[sattr]:
                if elem[sattr]['@aggtype'] in ["listkey"]:
                    skey = sattr
        return skey

    def _struct_sum_agg(self, oattr):
        akey = oattr.keys()[0]
        result = copy.deepcopy(oattr[akey])
        sname = None
        for sattr in result.keys():
            if sattr[0] != '@':
                sname = sattr
                break
        if not sname:
            return None
        cmap = {}
        for source,sval in oattr.iteritems():
            for attr, aval in sval[sname].iteritems():
                if aval['@type'] in ['i8',
                        'i16', 'i32', 'i64',
                        'byte', 'u8', 'u16', 'u32', 'u64']:
                    if attr not in cmap:
                        cmap[attr] = {}
                        cmap[attr]['@type'] = aval['@type']
                        cmap[attr]['#text'] = int(aval['#text'])
                    else:
                        cmap[attr]['#text'] += int(aval['#text'])
        for k,v in cmap.iteritems():
            v['#text'] = str(v['#text'])
        result[sname] = cmap
        return result

    def _elem_sum_agg(self, oattr):
        akey = oattr.keys()[0]
        result = copy.deepcopy(oattr[akey])
        count = 0
        for source in oattr.keys():
            count += int(oattr[source]['#text'])
        result['#text'] = str(count)
        return result

    def _list_union_agg(self, oattr):
        akey = oattr.keys()[0]
        result = {}
        for anno in oattr[akey].keys():
            if anno[0] == "@":
                result[anno] = oattr[akey][anno]
        itemset = set()
        sname = ParallelAggregator.get_list_name(oattr[akey])
        result['list'] = {}
        result['list'][sname] = []
        result['list']['@type'] = oattr[akey]['list']['@type']
        siz = 0
        for source in oattr.keys():
            if isinstance(oattr[source]['list'][sname], basestring):
                oattr[source]['list'][sname] = [oattr[source]['list'][sname]]
            for elem in oattr[source]['list'][sname]:
                hdelem = json.dumps(elem)
                if hdelem not in itemset:
                    itemset.add(hdelem)
                    result['list'][sname].append(elem)
                    siz += 1
        result['list']['@size'] = str(siz)
        
        return result

    def _map_union_agg(self, oattr):
        akey = oattr.keys()[0]
        result = {}
        for anno in oattr[akey].keys():
            if anno[0] == "@":
                result[anno] = oattr[akey][anno]
        result['map'] = {}
        result['map']['@key'] = 'string'
        result['map']['@value'] = oattr[akey]['map']['@value']
        result['map']['element'] = []

	sname = None
	for ss in oattr[akey]['map'].keys():
	    if ss[0] != '@':
		if ss != 'element':
		    sname = ss
                    result['map'][sname] = []

        siz = 0
        for source in oattr.keys():
            if sname is None:
		for subidx in range(0,int(oattr[source]['map']['@size'])):
		    print "map_union_agg Content %s" % (oattr[source]['map'])
		    result['map']['element'].append(source + ":" + \
			    json.dumps(oattr[source]['map']['element'][subidx*2]))
		    result['map']['element'].append(\
			    oattr[source]['map']['element'][(subidx*2) + 1])
		    siz += 1
            else:
                if not isinstance(oattr[source]['map']['element'], list):
                    oattr[source]['map']['element'] = [oattr[source]['map']['element']]
                if not isinstance(oattr[source]['map'][sname], list):
                    oattr[source]['map'][sname] = [oattr[source]['map'][sname]]
                
                for idx in range(0,int(oattr[source]['map']['@size'])):
                    result['map']['element'].append(source + ":" + \
                            json.dumps(oattr[source]['map']['element'][idx]))
                    result['map'][sname].append(\
                            oattr[source]['map'][sname][idx])
                    siz += 1
        
        result['map']['@size'] = str(siz)
             
        return result

    def _append_agg(self, oattr):
        akey = oattr.keys()[0]
        result = copy.deepcopy(oattr[akey])
        sname = ParallelAggregator.get_list_name(oattr[akey])
        result['list'][sname] = []
        siz = 0
        for source in oattr.keys():
            if not isinstance(oattr[source]['list'][sname], list):
                oattr[source]['list'][sname] = [oattr[source]['list'][sname]]
            for elem in oattr[source]['list'][sname]:
                result['list'][sname].append(elem)
                siz += 1
        result['list']['@size'] = str(siz)
        return result

    @staticmethod
    def _list_agg_attrs(item):
        for ctrs in item.keys():
            if '@aggtype'in item[ctrs]:
                if item[ctrs]['@aggtype'] in ["listkey"]:
                    continue
            if item[ctrs]['@type'] in ['i8', 'i16', 'i32', 'i64',
                                       'byte', 'u8', 'u16', 'u32', 'u64']:
                yield ctrs

    @staticmethod
    def consolidate_list(result, typ, objattr):
        applist = ParallelAggregator.get_list_name(
            result[typ][objattr])
        appkey = ParallelAggregator._get_list_key(
            result[typ][objattr]['list'][applist][0])

        # There is no listkey ; no consolidation is possible
        if len(appkey) == 0:
            return result

        # If the list's underlying struct has a listkey present,
        # we need to further aggregate entries that have the
        # same listkey
        mod_result = copy.deepcopy(result[typ][objattr])
        mod_result['list'][applist] = []
        res_size = 0
        mod_result['list']['@size'] = int(res_size)

        # Add up stats
        for items in result[typ][objattr]['list'][applist]:
            matched = False
            for res_items in mod_result['list'][applist]:
                if items[appkey]['#text'] in [res_items[appkey]['#text']]:
                    for ctrs in ParallelAggregator._list_agg_attrs(items):
                        res_items[ctrs]['#text'] += int(items[ctrs]['#text'])
                    matched = True
            if not matched:
                newitem = copy.deepcopy(items)
                for ctrs in ParallelAggregator._list_agg_attrs(items):
                    newitem[ctrs]['#text'] = int(items[ctrs]['#text'])
                mod_result['list'][applist].append(newitem)
                res_size += 1

        # Convert results back into strings
        for res_items in mod_result['list'][applist]:
            for ctrs in ParallelAggregator._list_agg_attrs(res_items):
                res_items[ctrs]['#text'] = str(res_items[ctrs]['#text'])
        mod_result['list']['@size'] = str(res_size)
        return mod_result

    def aggregate(self, key, flat, base_url = None):
        '''
        This function does parallel aggregation of this UVE's state.
        It aggregates across all sources and return the global state of the UVE
        '''
        result = {}
        ltyp = None
        objattr = None
        try:
            for typ in self._state[key].keys():
                ltyp = typ
                result[typ] = {}
                for objattr in self._state[key][typ].keys():
                    if self._is_elem_sum(self._state[key][typ][objattr]):
                        sume_res = self._elem_sum_agg(self._state[key][typ][objattr])
                        if flat:
                            result[typ][objattr] = \
                                OpServerUtils.uve_attr_flatten(sume_res)
                        else:
                            result[typ][objattr] = sume_res
                    elif self._is_struct_sum(self._state[key][typ][objattr]):
                        sums_res = self._struct_sum_agg(self._state[key][typ][objattr])
                        if flat:
                            result[typ][objattr] = \
                                OpServerUtils.uve_attr_flatten(sums_res)
                        else:
                            result[typ][objattr] = sums_res
                    elif self._is_list_union(self._state[key][typ][objattr]):
                        unionl_res = self._list_union_agg(
                            self._state[key][typ][objattr])
                        if flat:
                            result[typ][objattr] = \
                                OpServerUtils.uve_attr_flatten(unionl_res)
                        else:
                            result[typ][objattr] = unionl_res
                    elif self._is_map_union(self._state[key][typ][objattr]):
                        unionm_res = self._map_union_agg(
                            self._state[key][typ][objattr])
                        if flat:
                            result[typ][objattr] = \
                                OpServerUtils.uve_attr_flatten(unionm_res)
                        else:
                            result[typ][objattr] = unionm_res
                    elif self._is_append(self._state[key][typ][objattr]):
                        result[typ][objattr] = self._append_agg(
                            self._state[key][typ][objattr])
                        append_res = ParallelAggregator.consolidate_list(
                            result, typ, objattr)

                        if flat:
                            result[typ][objattr] =\
                                OpServerUtils.uve_attr_flatten(append_res)
                        else:
                            result[typ][objattr] = append_res

                    else:
                        default_res = self._default_agg(
                            self._state[key][typ][objattr])
                        if flat:
                            if (len(default_res) == 1):
                                result[typ][objattr] =\
                                    OpServerUtils.uve_attr_flatten(
                                        default_res[0][0])
                            else:
                                nres = []
                                for idx in range(len(default_res)):
                                    nres.append(default_res[idx])
                                    nres[idx][0] =\
                                        OpServerUtils.uve_attr_flatten(
                                            default_res[idx][0])
                                result[typ][objattr] = nres
                        else:
                            result[typ][objattr] = default_res
        except KeyError:
            pass
        except Exception as ex:
            print "Aggregation Error key %s type %s attr %s in %s" % \
                    (key, str(ltyp), str(objattr), str(self._state[key][typ][objattr]))
        return result

if __name__ == '__main__':
    uveserver = UVEServer(None, 0, None, None)
    gevent.spawn(uveserver.run())
    _, uve_state = json.loads(uveserver.get_uve("abc-corp:vn02", False))
    print json.dumps(uve_state, indent=4, sort_keys=True)
