#!/usr/bin/python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import argparse
import sys

import xml.etree.ElementTree as et
import StringIO
import re
import json

from distutils.sysconfig import get_python_lib
sys.path.append(get_python_lib() + '/vnc_cfg_api_server')

from operator import itemgetter, attrgetter
from vnc_api import vnc_api
from vnc_cfg_api_server import gen
from vnc_api.gen.resource_xsd import *
from vnc_api.gen.resource_common import *
from vnc_cfg_api_server.gen.vnc_ifmap_client_gen import *
from vnc_api.gen.vnc_api_client_gen import *

import pycassa
from pycassa.system_manager import *

# Global variables
vnc = None
outfile = None
"""
    Input is list of tuple dictionary <type, name, value, prop, refs>
    type  := project, domain ...
    value := dependent graph
    prop  := Properties of this node
    refs  := parenr->node reference meta data
"""


def mypretty(l, indent=0, verbose=0):
    l = sorted(l, key=itemgetter('type'))
    prop_fmt = '\n' + '    ' * (indent + 1)
    ref_fmt = '\n' + '    ' * indent
    for i in l:
        """ Prepare property string"""
        propstr = ''
        propstr2 = ''
        if i['props']:
            propstr = [p['name'] for p in i['props']]
            propstr = ' (' + ', '.join(propstr) + ')'
            if verbose >= 2:
                show_list = []
                for p in i['props']:
                    if p['name'] not in vnc_viewer.skip:
                        show_list.append(p)
                propstr2 = ['%s=%s' % (p['name'], p['value'])
                            for p in show_list]
                propstr2 = '\n'.join(propstr2)
                propstr2 = propstr2.split('\n')
                propstr2 = prop_fmt.join(propstr2)
        print '    ' * indent + '%s = '\
            % (i['type']) + str(i['name']) + propstr

        """ Prepare reference string"""
        ref_str = []
        if verbose >= 1 and i['refs']:
            ref_str = [r['value'] for r in i['refs']]
            ref_str = '\n'.join(ref_str)
            ref_str = ref_str.split('\n')
            ref_str = ref_fmt.join(ref_str)
            if len(ref_str) > 0:
                print '    ' * indent + ref_str

        if len(propstr2) > 0:
            print '    ' * (indent + 1) + propstr2
        if len(i['value']) > 0:
            mypretty(i['value'], indent + 1, verbose)
# end mypretty

"""
Find name in node list. Return the parent and subgraph
for subsequent traversal. Otherwise return def_node (typically None)
"""


def find_node(name, node_list, def_node):
    # traverse thru list of dict
    for item in node_list:
        if name == item['name']:
            return (item, item['value'])
    return (def_node, def_node)


def find_node_in_tree(fq_path, tree):
    path = fq_path.split(':')

    # Traverse until name is finished
    match, node = find_node('root', tree, tree)
    for name in path:
        match, n = find_node(name, node, None)
        if n is None:
            return None
        node = n
    return node
# end find_node_in_tree


def parse_config(soap_config):
    root = et.fromstring(soap_config)
    config = []
    for r_i in root.findall('*/*/*/resultItem'):
        ids = r_i.findall('identity')
        ident1 = ids[0].get('name')
        try:
            ident2 = ids[1].get('name')
        except IndexError:
            ident2 = None
        metas = r_i.find('metadata')

        # details
        outfile.write('\n' + et.tostring(r_i) + '\n')
        outfile.write('ident1 = %s\n' % (ident1))
        if ident2:
            outfile.write('ident2 = %s\n' % (ident2))
        if metas is not None:
            outfile.write('metas = %s\n' % (et.tostring(metas)))

        if not re.match("^contrail:", ident1):
            continue

        res = re.search("^contrail:([^:]+):(.*:)*(.*)$", ident1)
        type1 = res.group(1)
        name1 = res.group(3)
        id1 = ident1.split(':')
        # strip contrail, type
        id1 = id1[2:]
        outfile.write('Ident1 type = %s, name = %s\n' % (type1, name1))

        if ident2:
            res = re.search("^contrail:([^:]+):(.*:)*(.*)$", ident2)
            type2 = res.group(1)
            name2 = res.group(3)
            id2 = ident2.split(':')
            # strip contrail, type
            id2 = id2[2:]
            outfile.write('Ident2 type = %s, name = %s\n' % (type2, name2))

        # Traverse until name is finished
        match, node = find_node('root', config, config)
        for name in id1:
            match, n = find_node(name, node, None)
            if n is None:
                node.append(
                    {'type': type1, 'name': name1, 'value': [],
                     'props': [], 'refs': []})
                match = node[-1]
                node = node[-1]['value']
                break
            node = n

        node1 = node

        if ident2:
            match, n = find_node(name2, node1, None)
            if n is None:
                match = {'type': type2, 'name': name2,
                         'value': [], 'props': [], 'refs': []}
                node1.append(match)

        # attach property or reference info if available
        if metas is None:
            continue

        for meta in metas:
            meta_name = re.sub('{.*}', '', meta.tag)
            outfile.write('Handling meta = %s\n' % (meta_name))
            if ident2:
                if meta_name in link_name_to_xsd_type:
                    obj = eval(link_name_to_xsd_type[meta_name])()
                    obj.build(meta)
                    obj_json = json.dumps(
                        obj,
                        default=lambda o: dict(
                            (k, v) for k,
                            v in o.__dict__.iteritems()), indent=4)
                    outfile.write(
                        'Attaching Reference %s to Id %s\n'
                        % (meta_name, ident2))
                    outfile.write('JSON %s = %s\n' % (meta_name, obj_json))
                    match['refs'].append(
                        {'name': '%s' % (meta_name), 'value': obj_json})
            else:
                if meta_name in vnc.prop_name_to_xsd_type:
                    obj = eval(vnc.prop_name_to_xsd_type[meta_name])()
                    obj.build(meta)
                    obj_json = json.dumps(
                        obj,
                        default=lambda o: dict(
                            (k, v) for k,
                            v in o.__dict__.iteritems()), indent=4)
                    outfile.write(
                        'Attaching Property %s to Id %s\n'
                        % (meta_name, ident1))
                    outfile.write('JSON %s = %s\n' % (meta_name, obj_json))
                    match['props'].append(
                        {'name': '%s' % (meta_name), 'value': obj_json})

    return config
# end parse_config


class IfmapClient():

    def __init__(self, ifmap_srv_ip, ifmap_srv_port, uname, passwd):
        """
        .. attention:: username/passwd from right place
        """
        self._CONTRAIL_XSD = "http://www.contrailsystems.com/vnc_cfg.xsd"

        self._NAMESPACES = {
            'a': 'http://www.w3.org/2003/05/soap-envelope',
            'b': 'http://www.trustedcomputinggroup.org/2010/IFMAP/2',
            'c': self._CONTRAIL_XSD
        }

        namespaces = {
            'env':   "http://www.w3.org/2003/05/soap-envelope",
            'ifmap':   "http://www.trustedcomputinggroup.org/2010/IFMAP/2",
            'meta':   "http://www.trustedcomputinggroup.org/"
                      "2010/IFMAP-METADATA/2",
            'contrail':   self._CONTRAIL_XSD
        }

        mapclient = client(("%s" % (ifmap_srv_ip), "%s" % (ifmap_srv_port)),
                           uname, passwd, namespaces)
        result = mapclient.call('newSession', NewSessionRequest())
        mapclient.set_session_id(newSessionResult(result).get_session_id())
        mapclient.set_publisher_id(newSessionResult(result).get_publisher_id())

        self._mapclient = mapclient
    # end __init__

    def _search(self, start_id, match_meta=None, result_meta=None,
                max_depth=1):
        # set ifmap search parmeters
        srch_params = {}
        srch_params['max-depth'] = str(max_depth)
        srch_params['max-size'] = '50000000'

        if match_meta is not None:
            srch_params['match-links'] = match_meta

        if result_meta is not None:
            # all => don't set result-filter, so server returns all id + meta
            if result_meta == "all":
                pass
            else:
                srch_params['result-filter'] = result_meta
        else:
            # default to return match_meta metadata types only
            srch_params['result-filter'] = match_meta

        mapclient = self._mapclient
        srch_req = SearchRequest(mapclient.get_session_id(), start_id,
                                 search_parameters=srch_params
                                 )
        result = mapclient.call('search', srch_req)

        return result
        # end _search

    def ifmap_read(self, ifmap_id, srch_meta, result_meta, field_names=None):
        start_id = str(
            Identity(name=ifmap_id, type='other', other_type='extended'))
        srch_result = self._search(
            start_id, srch_meta, result_meta, max_depth=10)
        return srch_result
        # end ifmap_read

# end class IfmapClient


class VncViewer():

    def parse_args(self):
        # Eg. python vnc_ifmap_view.py 192.168.1.17 8443 test2 test2
        parser = argparse.ArgumentParser(
            description="Display IFMAP configuration")
        parser.add_argument(
            'ifmap_server_ip', help="IP address of ifmap server")
        parser.add_argument('ifmap_server_port', help="Port of ifmap server")
        parser.add_argument(
            'ifmap_username', help="Username known to ifmap server")
        parser.add_argument(
            'ifmap_password', help="Password known to ifmap server")
        parser.add_argument('-v', type=int, default=0, choices=range(0, 3),
                            help="Turn verbosity on. Default is 0")
        """
        parser.add_argument('-n', '--node', default=None,
            help = "Start node (fully qualified name such as
                                default-domain:admin:vn2")
        parser.add_argument('-s', '--skip', action='append',
            help = "Skip property (such as id-perms)")
        """
        self._args = parser.parse_args()
        self.verbose = self._args.v
        """
        self.start_node = self._args.node
        self.skip = self._args.skip
        """
        self.start_node = None
        self.skip = ['id-perms']
        print 'MAP server connection  = %s:%s'\
            % (self._args.ifmap_server_ip, self._args.ifmap_server_port)
        print 'MAP server credentials = %s:%s'\
            % (self._args.ifmap_username,  self._args.ifmap_password)
        print 'Start node = %s' % (self.start_node)
        print 'Skip List = %s' % (self.skip)
        print 'Verbose = %s' % (self.verbose)
        print ''
    # end parse_args

    def db_connect(self):
        ifmap_ip = self._args.ifmap_server_ip
        ifmap_port = self._args.ifmap_server_port
        user = self._args.ifmap_username
        passwd = self._args.ifmap_password

        # ifmap interface
        db_conn = IfmapClient(ifmap_ip, ifmap_port, user, passwd)
        self._db_conn = db_conn
    # end db_connect

def main():
    vnc_viewer = VncViewer()
    vnc_viewer.parse_args()
    vnc_viewer.db_connect()

    #vnc = VncApi('admin', 'contrail123', 'admin', '127.0.0.1', '8082')
    global vnc
    global outfile
    vnc = VncApiClientGen(obj_serializer=None)
    outfile = file("debug.txt", "w")

    """ sample search metas
     srch_meta = 'contrail:config-root-domain' (retunn only domains)
     srch_meta = ' or '.join(['contrail:config-root-domain',
                 'contrail:config-root-virtual-router']) (domain or virtual-router)
     srch_meta = 'contrail:config-root-domain or
                  contrail:config-root-virtual-router' (same as above)
     srch_meta = 'contrail:domain-project' (search all projects)
     srch_meta = None (search everything)
    """

    srch_meta = None
    result_meta = 'all'
    soap_result = vnc_viewer._db_conn.ifmap_read(
        'contrail:config-root:root', srch_meta, result_meta)
    config = parse_config(soap_result)
    if vnc_viewer.start_node is None:
        node = config[0]['value']
    else:
        node = find_node_in_tree(vnc_viewer.start_node, config)
    mypretty(node, verbose=vnc_viewer.verbose)

if __name__ == '__main__':
    main()
