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
from opserver_util import OpServerUtils
import re
from gevent.coros import BoundedSemaphore
from pysandesh.util import UTCTimestampUsec

class UVEServer(object):

    def __init__(self, redis_uve_server, logger):
        self._local_redis_uve = redis_uve_server
        self._redis_uve_list = []
        self._logger = logger
        self._sem = BoundedSemaphore(1)
        self._redis = None
        if self._local_redis_uve:
            self._redis = redis.StrictRedis(self._local_redis_uve[0],
                                            self._local_redis_uve[1], db=1)
    #end __init__

    def update_redis_uve_list(self, redis_uve_list):
        self._redis_uve_list = redis_uve_list
    # end update_redis_uve_list 

    def fill_redis_uve_info(self, redis_uve_info):
        redis_uve_info.ip = self._local_redis_uve[0]
        redis_uve_info.port = self._local_redis_uve[1]
        try:
            self._redis.ping()
        except redis.exceptions.ConnectionError:
            redis_uve_info.status = 'DisConnected'
        else:
            redis_uve_info.status = 'Connected'
    #end fill_redis_uve_info

    @staticmethod
    def merge_previous(state, key, typ, attr, prevdict):
        print "%s New    val is %s" % (attr, prevdict)
        nstate = copy.deepcopy(state)
        if UVEServer._is_agg_item(prevdict):
            count = int(state[key][typ][attr]['previous']['#text'])
            count += int(prevdict['#text'])
            nstate[key][typ][attr]['previous']['#text'] = str(count)

        if UVEServer._is_agg_list(prevdict):
            sname = ParallelAggregator.get_list_name(
                state[key][typ][attr]['previous'])
            count = len(prevdict['list'][sname]) + \
                len(state[key][typ][attr]['previous']['list'][sname])
            nstate[key][typ][attr]['previous']['list'][sname].extend(
                prevdict['list'][sname])
            nstate[key][typ][attr]['previous']['list']['@size'] = \
                str(count)

            tstate = {}
            tstate[typ] = {}
            tstate[typ][attr] = copy.deepcopy(
                nstate[key][typ][attr]['previous'])
            nstate[key][typ][attr]['previous'] =\
                ParallelAggregator.consolidate_list(tstate, typ, attr)

        print "%s Merged val is %s"\
            % (attr, nstate[key][typ][attr]['previous'])
        return nstate

    def run(self):
        lck = False
        while True:
            try:
                k, value = self._redis.brpop("DELETED")
                self._sem.acquire()
                lck = True
                self._logger.debug("%s del received for " % value)
                # value is of the format: 
                # DEL:<key>:<src>:<node-type>:<module>:<instance-id>:<message-type>:<seqno>
                info = value.rsplit(":", 6)
                key = info[0].split(":", 1)[1]
                typ = info[5]

                existing = self._redis.hgetall("PREVIOUS:" + key + ":" + typ)
                tstate = {}
                tstate[key] = {}
                tstate[key][typ] = {}
                state = UVEServer.convert_previous(existing, tstate, key, typ)

                for attr, hval in self._redis.hgetall(value).iteritems():
                    snhdict = xmltodict.parse(hval)

                    if UVEServer._is_agg_list(snhdict[attr]):
                        if snhdict[attr]['list']['@size'] == "0":
                            continue
                        if snhdict[attr]['list']['@size'] == "1":
                            sname = ParallelAggregator.get_list_name(
                                snhdict[attr])
                            if not isinstance(
                                    snhdict[attr]['list'][sname], list):
                                snhdict[attr]['list'][sname] = \
                                    [snhdict[attr]['list'][sname]]

                    if (attr not in state[key][typ]):
                        # There is no existing entry for the UVE
                        vstr = json.dumps(snhdict[attr])
                    else:
                        # There is an existing entry
                        # Merge the new entry with the existing one
                        state = UVEServer.merge_previous(
                            state, key, typ, attr, snhdict[attr])
                        vstr = json.dumps(state[key][typ][attr]['previous'])

                        # Store the merged result back in the database
                    self._redis.sadd("PUVES:" + typ, key)
                    self._redis.sadd("PTYPES:" + key, typ)
                    self._redis.hset("PREVIOUS:" + key + ":" + typ, attr, vstr)

                self._redis.delete(value)
            except redis.exceptions.ConnectionError:
                if lck:
                    self._sem.release()
                    lck = False
                gevent.sleep(5)
            else:
                if lck:
                    self._sem.release()
                    lck = False
                self._logger.debug("Deleted %s" % value)
                self._logger.debug("UVE %s Type %s" % (key, typ))

    @staticmethod
    def _is_agg_item(attr):
        if attr['@type'] in ['i8', 'i16', 'i32', 'i64', 'byte',
                             'u8', 'u16', 'u32', 'u64']:
            if '@aggtype' in attr:
                if attr['@aggtype'] == "counter":
                    return True
        return False

    @staticmethod
    def _is_agg_list(attr):
        if attr['@type'] in ['list']:
            if '@aggtype' in attr:
                if attr['@aggtype'] == "append":
                    return True
        return False

    @staticmethod
    def convert_previous(existing, state, key, typ, afilter=None):
        # Take the existing delete record, and load it into the state dict
        for attr, hval in existing.iteritems():
            hdict = json.loads(hval)

            if afilter is not None and len(afilter):
                if attr not in afilter:
                    continue

            # When recording deleted attributes, only record those
            # for which delete-time aggregation is needed
            if UVEServer._is_agg_item(hdict):
                if (typ not in state[key]):
                    state[key][typ] = {}
                if (attr not in state[key][typ]):
                    state[key][typ][attr] = {}
                state[key][typ][attr]["previous"] = hdict

            # For lists that require delete-time aggregation, we need
            # to normailize lists of size 1, and ignore those of size 0
            if UVEServer._is_agg_list(hdict):
                if hdict['list']['@size'] != "0":
                    if (typ not in state[key]):
                        state[key][typ] = {}
                    if (attr not in state[key][typ]):
                        state[key][typ][attr] = {}
                    state[key][typ][attr]["previous"] = hdict
                if hdict['list']['@size'] == "1":
                    sname = ParallelAggregator.get_list_name(hdict)
                    if not isinstance(hdict['list'][sname], list):
                        hdict['list'][sname] = [hdict['list'][sname]]

        return state

    def get_uve(self, key, flat, sfilter=None, mfilter=None, tfilter=None, multi=False):
        state = {}
        state[key] = {}
        statdict = {}
        for redis_uve in self._redis_uve_list:
            redish = redis.StrictRedis(host=redis_uve[0],
                                       port=redis_uve[1], db=1)
            try:
                empty = True
                qmap = {}
                for origs in redish.smembers("ORIGINS:" + key):
                    info = origs.rsplit(":", 1)
                    sm = info[0].split(":", 1)
                    source = sm[0]
                    if sfilter is not None:
                        if sfilter != source:
                            continue
                    mdule = sm[1]
                    if mfilter is not None:
                        if mfilter != mdule:
                            continue
                    dsource = source + ":" + mdule

                    typ = info[1]
                    if tfilter is not None:
                        if typ not in tfilter:
                            continue

                    odict = redish.hgetall("VALUES:" + key + ":" + origs)

                    afilter_list = set()
                    if tfilter is not None:
                        afilter_list = tfilter[typ]
                    for attr, value in odict.iteritems():
                        if len(afilter_list):
                            if attr not in afilter_list:
                                continue

                        if typ not in state[key]:
                            state[key][typ] = {}

                        if value[0] == '<':
                            empty = False
                            snhdict = xmltodict.parse(value)
                            if snhdict[attr]['@type'] == 'list':
                                if snhdict[attr]['list']['@size'] == '0':
                                    continue
                                elif snhdict[attr]['list']['@size'] == '1':
                                    sname = ParallelAggregator.get_list_name(
                                        snhdict[attr])
                                    if not isinstance(
                                        snhdict[attr]['list'][sname], list):
                                        snhdict[attr]['list'][sname] = [
                                            snhdict[attr]['list'][sname]]
                        else:
                            if not flat:
                                continue
                            if typ not in statdict:
                                statdict[typ] = {}
                            statdict[typ][attr] = []
                            statsattr = json.loads(value)
                            for elem in statsattr:
                                #import pdb; pdb.set_trace()
                                edict = {}
                                if elem["rtype"] == "list":
                                    elist = redish.lrange(elem["href"], 0, -1)
                                    for eelem in elist:
                                        jj = json.loads(eelem).items()
                                        edict[jj[0][0]] = jj[0][1]
                                elif elem["rtype"] == "zset":
                                    elist = redish.zrange(
                                        elem["href"], 0, -1, withscores=True)
                                    for eelem in elist:
                                        tdict = json.loads(eelem[0])
                                        tval = long(tdict["ts"])
                                        dt = datetime.datetime.utcfromtimestamp(
                                            float(tval) / 1000000)
                                        tms = (tval % 1000000) / 1000
                                        tstr = dt.strftime('%Y %b %d %H:%M:%S')
                                        edict[tstr + "." + str(tms)] = eelem[1]
                                elif elem["rtype"] == "hash":
                                    elist = redish.hgetall(elem["href"])
                                    edict = elist
                                elif elem["rtype"] == "query":
                                    if sfilter is None and mfilter is None and not multi:
                                        qdict = {}
                                        qdict["table"] = elem["aggtype"]
                                        qdict["select_fields"] = elem["select"]
                                        qdict["where"] =[[{"name":"name",
                                            "value":key.split(":",1)[1],
                                            "op":1}]]
                                        qmap[elem["aggtype"]] = {"query":qdict,
                                            "type":typ, "attr":attr}
                                    # For the stats query case, defer processing
                                    continue

                                statdict[typ][attr].append(
                                    {elem["aggtype"]: edict})
                            continue

                        # print "Attr %s Value %s" % (attr, snhdict)
                        if attr not in state[key][typ]:
                            state[key][typ][attr] = {}
                        if dsource in state[key][typ][attr]:
                            print "Found Dup %s:%s:%s:%s:%s = %s" % \
                                (key, typ, attr, source, mdule, state[
                                key][typ][attr][dsource])
                        state[key][typ][attr][dsource] = snhdict[attr]
                
                if not empty:
                    url = OpServerUtils.opserver_query_url(
                        self._local_redis_uve[0],
                        str(8081))
                    for t,q in qmap.iteritems():
                        try:
                            q["query"]["end_time"] = OpServerUtils.utc_timestamp_usec()
                            q["query"]["start_time"] = qdict["end_time"] - (3600 * 1000000)
                            json_str = json.dumps(q["query"])
                            resp = OpServerUtils.post_url_http(url, json_str, True)
                            if resp is not None:
                                edict = json.loads(resp)
                                edict = edict['value']
                                statdict[q["type"]][q["attr"]].append(
                                    {t: edict})
                        except Exception as e:
                            print "Stats Query Exception:" + str(e)
                        
                if sfilter is None and mfilter is None:
                    for ptyp in redish.smembers("PTYPES:" + key):
                        afilter = None
                        if tfilter is not None:
                            if ptyp not in tfilter:
                                continue
                            afilter = tfilter[ptyp]
                        existing = redish.hgetall("PREVIOUS:" + key + ":" + ptyp)
                        nstate = UVEServer.convert_previous(
                            existing, state, key, ptyp, afilter)
                        state = copy.deepcopy(nstate)

                pa = ParallelAggregator(state)
                rsp = pa.aggregate(key, flat)
            except redis.exceptions.ConnectionError:
                self._logger.error("Failed to connect to redis-uve: %s:%d" \
                                   % (redis_uve[0], redis_uve[1]))
            except Exception as e:
                self._logger.error("Exception: %s" % e)
                return {}
            else:
                self._logger.debug("Computed %s" % key)

        for k, v in statdict.iteritems():
            if k in rsp:
                mp = dict(v.items() + rsp[k].items())
                statdict[k] = mp

        return dict(rsp.items() + statdict.items())
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

    def multi_uve_get(self, key, flat, kfilter, sfilter, mfilter, tfilter):
        tbl_uve = key.split(':', 1)
        table = tbl_uve[0]

        # get_uve_list cannot handle attribute names very efficiently,
        # so we don't pass them here
        k1_filter = [tbl_uve[1]]
        uve_list = self.get_uve_list(table, k1_filter, sfilter,
                                     mfilter, tfilter, False)
        if kfilter is not None:
            patterns = set()
            for filt in kfilter:
                patterns.add(self.get_uve_regex(filt))
        for uve_name in uve_list:
            if kfilter is not None:
                kfilter_match = False
                for pattern in patterns:
                    if pattern.match(uve_name):
                        kfilter_match = True
                        break
                if not kfilter_match:
                    continue
            uve_val = self.get_uve(
                table + ':' + uve_name, flat,
                sfilter, mfilter, tfilter, True)
            if uve_val == {}:
                continue
            else:
                uve = {'name': uve_name, 'value': uve_val}
                yield uve
    # end multi_uve_get

    def get_uve_list(self, key, kfilter, sfilter,
                     mfilter, tfilter, parse_afilter):
        uve_list = set()
        if kfilter is not None:
            patterns = set()
            for filt in kfilter:
                patterns.add(self.get_uve_regex(filt))
        for redis_uve in self._redis_uve_list:
            redish = redis.StrictRedis(host=redis_uve[0],
                                       port=redis_uve[1], db=1)
            try:
                for entry in redish.smembers("TABLE:" + key):
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
                    node_type = info[2]
                    mdule = info[3]
                    if mfilter is not None:
                        if mfilter != mdule:
                            continue
                    inst = info[4] 
                    typ = info[5]
                    if tfilter is not None:
                        if typ not in tfilter:
                            continue
                    if parse_afilter:
                        if tfilter is not None and len(tfilter[typ]):
                            valkey = "VALUES:" + key + ":" + uve_key + ":" + \
                                 src + ":" + node_type + ":" + mdule + \
                                 ":" + inst + ":" + typ
                            for afilter in tfilter[typ]:
                                attrval = redish.hget(valkey, afilter)
                                if attrval is not None:
                                    break
                            if attrval is None:
                                continue
                    uve_list.add(uve_key)
            except redis.exceptions.ConnectionError:
                self._logger.error('Failed to connect to redis-uve: %s:%d' \
                                   % (redis_uve[0], redis_uve[1]))
            except Exception as e:
                self._logger.error('Exception: %s' % e)
                return set()
        return uve_list
    # end get_uve_list

# end UVEServer


class ParallelAggregator:

    def __init__(self, state):
        self._state = state

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

    def _is_sum(self, oattr):
        akey = oattr.keys()[0]
        if '@aggtype' not in oattr[akey]:
            return False
        if oattr[akey]['@aggtype'] in ["sum"]:
            return True
        if oattr[akey]['@type'] in ['i8', 'i16', 'i32', 'i64',
                                    'byte', 'u8', 'u16', 'u32', 'u64']:
            if oattr[akey]['@aggtype'] in ["counter"]:
                return True
        return False

    def _is_union(self, oattr):
        akey = oattr.keys()[0]
        if not oattr[akey]['@type'] in ["list"]:
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

    def _sum_agg(self, oattr):
        akey = oattr.keys()[0]
        result = copy.deepcopy(oattr[akey])
        count = 0
        for source in oattr.keys():
            count += int(oattr[source]['#text'])
        result['#text'] = str(count)
        return result

    def _union_agg(self, oattr):
        akey = oattr.keys()[0]
        result = copy.deepcopy(oattr[akey])
        itemset = set()
        sname = ParallelAggregator.get_list_name(oattr[akey])
        result['list'][sname] = []

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

    def aggregate(self, key, flat):
        '''
        This function does parallel aggregation of this UVE's state.
        It aggregates across all sources and return the global state of the UVE
        '''
        result = {}
        try:
            for typ in self._state[key].keys():
                result[typ] = {}
                for objattr in self._state[key][typ].keys():
                    if self._is_sum(self._state[key][typ][objattr]):
                        sum_res = self._sum_agg(self._state[key][typ][objattr])
                        if flat:
                            result[typ][objattr] = \
                                OpServerUtils.uve_attr_flatten(sum_res)
                        else:
                            result[typ][objattr] = sum_res
                    elif self._is_union(self._state[key][typ][objattr]):
                        union_res = self._union_agg(
                            self._state[key][typ][objattr])
                        if flat:
                            result[typ][objattr] = \
                                OpServerUtils.uve_attr_flatten(union_res)
                        else:
                            result[typ][objattr] = union_res
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
        return result

if __name__ == '__main__':
    uveserver = UVEServer(None, 0, None, None)
    gevent.spawn(uveserver.run())
    uve_state = json.loads(uveserver.get_uve("abc-corp:vn02", False))
    print json.dumps(uve_state, indent=4, sort_keys=True)
