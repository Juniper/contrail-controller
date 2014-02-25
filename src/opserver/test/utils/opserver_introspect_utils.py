#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import sys
vizdtestdir = sys.path[0]

import urllib2
import xmltodict
import json
import requests
import socket
from lxml import etree
from verification_util import *
from opserver_results import *
from opserver.opserver_util import OpServerUtils


class VerificationOpsSrv (VerificationUtilBase):

    def __init__(self, ip, port=8081):
        super(VerificationOpsSrv, self).__init__(ip, port)

    def get_ops_vm(self, vm='default-virtual-machine'):
        vm_dict = self.dict_get('analytics/virtual-machine/' + vm)
        return OpVMResult(vm_dict)

    def get_ops_vn(self, vn='default-virtual-network'):
        res = None
        try:
            vn_dict = self.dict_get('analytics/virtual-network/' + vn)
            res = OpVNResult(vn_dict)
        except Exception as e:
            print e
        finally:
            return res

    def get_ops_collector(self, col=None):
        if (col is None):
            col = socket.gethostname()
        res = None
        try:
            #import pdb; pdb.set_trace()
            col_dict = self.dict_get('analytics/collector/' + col)
            res = OpCollectorResult(col_dict)
        except Exception as e:
            print e
        finally:
            return res

    def send_tracebuffer_req(self, src, mod, instance, buf_name):
        return self.dict_get('analytics/send-tracebuffer/%s/%s/%s/%s' \
                             % (src, mod, instance, buf_name))

    def get_table_column_values(self, table, col_name):
        return self.dict_get('analytics/table/%s/column-values/%s' \
                             % (table, col_name)) 

    def uve_query(self, query):
        return self.dict_get('analytics/uves/%s' % query)

    def get_redis_uve_info(self):
        path = 'Snh_RedisUVERequest'
        xpath = '/RedisUVEResponse/redis_uve_info'
        p = self.dict_get(path, XmlDrv)
        return EtreeToDict(xpath).get_all_entry(p)

    def post_query_json(self, json_str, sync=True):
        '''
        this module is to support raw query given in json format
        '''
        res = None
        try:
            flows_url = OpServerUtils.opserver_query_url(self._ip, str(self._port))
            print flows_url
            print "query is: ", json_str
            res = []
            resp = OpServerUtils.post_url_http(flows_url, json_str, sync)
            if sync:
                if resp is not None:
                    res = json.loads(resp)
                    res = res['value']
            else: 
                if resp is not None:
                    resp = json.loads(resp)
                    qid = resp['href'].rsplit('/', 1)[1]
                    result = OpServerUtils.get_query_result(self._ip, str(self._port), qid, 30)
                    for item in result:
                        res.append(item)
        except Exception as e:
            print str(e) 
        finally:
            return res        

    def post_query(self, table, start_time=None, end_time=None,
                   select_fields=None, where_clause=None,
                   sort_fields=None, sort=None, limit=None,
                   filter=None, sync=True,dir=None):
        res = None
        try:
            flows_url = OpServerUtils.opserver_query_url(
                self._ip, str(self._port))
            print flows_url
            query_dict = OpServerUtils.get_query_dict(
                table, start_time, end_time,
                select_fields,
                where_clause,
                sort_fields, sort, limit, filter, dir)

            print json.dumps(query_dict)
            res = []
            resp = OpServerUtils.post_url_http(
                flows_url, json.dumps(query_dict), sync)
            if sync:
                if resp is not None:
                    res = json.loads(resp)
                    res = res['value']
            else:
                if resp is not None:
                    resp = json.loads(resp)
                    qid = resp['href'].rsplit('/', 1)[1]
                    result = OpServerUtils.get_query_result(
                        self._ip, str(self._port), qid, 30)
                    for item in result:
                        res.append(item)
        except Exception as e:
            print str(e)
        finally:
            return res

if __name__ == '__main__':
    vns = VerificationOpsSrv('127.0.0.1')

    vn = vns.get_ops_vn(vn='abc-corp:vn02')

    print "*** Verify VN Cfg ***"

    print vn.get_attr('Config', 'attached_policies', 'abc-default-policy')
    '''
    [{u'vnp_major': u'10', u'vnp_name': u'abc-default-policy',
      u'vnp_minor': u'50'}]
    '''

    print vn.get_attr('Config', 'connected_networks')
    '''
    [u'abc-corp:vn04']
    '''

    print vn.get_attr('Config', 'total_interfaces')
    '''
    10
    '''

    print vn.get_attr('Config', 'total_acl_rules')
    '''
    60
    '''

    print "*** Verify VN Agt ***"

    print vn.get_attr('Agent', 'total_acl_rules')
    '''
    55
    '''

    print vn.get_attr('Agent', 'in_tpkts')
    '''
    240
    '''

    print vn.get_attr('Agent', 'in_stats', 'abc-corp:map-reduce-02')
    '''
    [{u'bytes': u'7200', u'other_vn': u'abc-corp:map-reduce-02',
      u'tpkts': u'60'}]
    '''

    vm = vns.get_ops_vm(vm='abc-corp:vm-web-fe01')

    print "*** Verify VM Cfg ***"

    print vm.get_attr('Config', 'vrouter')
    '''
    rack01-host04
    '''

    print vm.get_attr('Config', 'attached_groups')
    '''
    [u'abc-grp01']
    '''

    print vm.get_attr('Config', 'interface_list', 'abc-corp:vn-fe')
    '''
    [{u'virtual_network': u'abc-corp:vn-fe', u'ip_address': u'10.1.1.2',
      u'floating_ips': [u'67.1.1.2', u'67.1.1.3']}]
    '''

    print "*** Verify VM Agt ***"

    print vm.get_attr('Agent', 'vrouter')
    '''
    rack01-host04
    '''

    print vm.get_attr('Agent', 'attached_groups')
    '''
    [u'abc-grp01']
    '''

    print vm.get_attr('Agent', 'interface_list')
    '''
    [{u'in_bytes': u'1000', u'out_bytes': u'10000',
      u'floating_ips': [u'67.1.1.2', u'67.1.1.3'],
      u'out_pkts': u'20', u'virtual_network': u'abc-corp:vn-fe',
      u'in_pkts': u'5', u'ip_address': u'10.1.1.2'}]
    '''

    col = vns.get_ops_collector()

    print col.get_attr('Analytics', 'generator_infos')
    '''
    [{u'gen_attr': {u'http_port': u'8089', u'in_clear': u'false',
                    u'pid': u'57160', u'connects': u'1', u'clears': u'1',
                    u'resets': u'0'},
      u'source': u'sa-nc-mfg-30.static.jnpr.net',
      u'msgtype_stats': {u'SandeshStats':
                             [{u'bytes': u'1363005',
                               u'messages': u'431',
                               u'message_type': u'CollectorInfo'}]},
                         u'module_id': u'Collector'},
     {u'gen_attr': {u'http_port': u'0', u'in_clear': u'false',
                    u'pid': u'0', u'connects': u'1', u'clears': u'0',
                    u'resets': u'0'},
      u'source': u'sa-nc-mfg-30.static.jnpr.net', u'msgtype_stats': {},
      u'module_id': u'OpServer'},
     {u'gen_attr': {u'http_port': u'8091', u'in_clear': u'false',
                    u'pid': u'57200', u'connects': u'2', u'clears': u'2',
                    u'resets': u'1'},
      u'source': u'sa-nc-mfg-30.static.jnpr.net',
      u'msgtype_stats': {u'SandeshStats': [{u'bytes': u'16771',
                                            u'messages': u'66',
                                            u'message_type': u'QELog'},
                                           {u'bytes': u'12912',
                                            u'messages': u'32',
                                            u'message_type': u'QEQueryLog'}]},
      u'module_id': u'QueryEngine'}]
    '''

    print col.get_attr('Analytics', 'generator_infos',
                       [('module_id', 'OpServer'),
                        ('source', "sa-nc-mfg-30.static.jnpr.net")])
    '''
    [{u'gen_attr': {u'http_port': u'0', u'in_clear': u'false', u'pid': u'0',
                    u'connects': u'1', u'clears': u'0', u'resets': u'0'},
      u'source': u'sa-nc-mfg-30.static.jnpr.net', u'msgtype_stats': {},
      u'module_id': u'OpServer'}]
    '''
    print col.get_attr('Analytics', 'cpu_info')
    '''
{u'num_cpu': u'4', u'cpu_share': u'0.00833056',
 u'meminfo': {u'virt': u'2559582208', u'peakvirt': u'2559582208',
              u'res': u'2805760'}}
    '''
