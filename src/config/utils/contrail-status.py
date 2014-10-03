#!/usr/bin/python
#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

from optparse import OptionParser
import subprocess
import os
import glob
import platform
import ConfigParser
import socket
import requests
from StringIO import StringIO
from lxml import etree
from sandesh_common.vns.constants import ServiceHttpPortMap, \
    NodeUVEImplementedServices

try:
    subprocess.check_call(["dpkg-vendor", "--derives-from", "debian"])
    distribution = 'debian'
except:
    distribution = 'centos'

class EtreeToDict(object):
    """Converts the xml etree to dictionary/list of dictionary."""

    def __init__(self, xpath):
        self.xpath = xpath
    #end __init__

    def _handle_list(self, elems):
        """Handles the list object in etree."""
        a_list = []
        for elem in elems.getchildren():
            rval = self._get_one(elem, a_list)
            if 'element' in rval.keys():
                a_list.append(rval['element'])
            elif 'list' in rval.keys():
                a_list.append(rval['list'])
            else:
                a_list.append(rval)

        if not a_list:
            return None
        return a_list
    #end _handle_list

    def _get_one(self, xp, a_list=None):
        """Recrusively looks for the entry in etree and converts to dictionary.

        Returns a dictionary.
        """
        val = {}

        child = xp.getchildren()
        if not child:
            val.update({xp.tag: xp.text})
            return val

        for elem in child:
            if elem.tag == 'list':
                val.update({xp.tag: self._handle_list(elem)})
            else:
                rval = self._get_one(elem, a_list)
                if elem.tag in rval.keys():
                    val.update({elem.tag: rval[elem.tag]})
                else:
                    val.update({elem.tag: rval})
        return val
    #end _get_one

    def get_all_entry(self, path):
        """All entries in the etree is converted to the dictionary

        Returns the list of dictionary/didctionary.
        """
        xps = path.xpath(self.xpath)

        if type(xps) is not list:
            return self._get_one(xps)

        val = []
        for xp in xps:
            val.append(self._get_one(xp))
        if len(val) == 1:
            return val[0]
        return val
    #end get_all_entry

    def find_entry(self, path, match):
        """Looks for a particular entry in the etree.
        Returns the element looked for/None.
        """
        xp = path.xpath(self.xpath)
        f = filter(lambda x: x.text == match, xp)
        if len(f):
            return f[0].text
        return None
    #end find_entry

#end class EtreeToDict

class IntrospectUtil(object):
    def __init__(self, ip, port, debug):
        self._ip = ip
        self._port = port
        self._debug = debug
    #end __init__

    def _mk_url_str(self, path):
        return "http://%s:%d/%s" % (self._ip, self._port, path)
    #end _mk_url_str

    def _load(self, path):
        url = self._mk_url_str(path)
        try:
            resp = requests.get(url, timeout=0.5)
            if resp.status_code == requests.codes.ok:
                return etree.fromstring(resp.text)
            else:
                if self._debug:
                    print 'URL: %s : HTTP error: %s' % (url, str(resp.status_code))
                return None
        except requests.ConnectionError, e:
            if self._debug:
                print 'URL: %s : Socket Connection error : %s' % (url, str(e))
            return None
        except (requests.Timeout, socket.timeout) as te:
            if self._debug:
                print 'URL: %s : Timeout error : %s' % (url, str(te))
            return None
    #end _load

    def get_uve(self, tname):
        path = 'Snh_SandeshUVECacheReq?x=%s' % (tname)
        xpath = './/' + tname
        p = self._load(path)
        if p is not None:
            return EtreeToDict(xpath).get_all_entry(p)
        else:
            if self._debug:
                print 'UVE: %s : not found' % (path)
            return None
    #end get_uve

#end class IntrospectUtil

def service_installed(svc):
    if distribution == 'debian':
        cmd = 'initctl show-config ' + svc
    else:
        cmd = 'chkconfig --list ' + svc
    with open(os.devnull, "w") as fnull:
        return not subprocess.call(cmd.split(), stdout=fnull, stderr=fnull)

def service_bootstatus(svc):
    if distribution == 'debian':
        cmd = 'initctl show-config ' + svc
        cmdout = subprocess.Popen(cmd.split(), stdout=subprocess.PIPE).communicate()[0]
        if cmdout.find('  start on') != -1:
            return ''
        else:
            return ' (disabled on boot)'
    else:
        cmd = 'chkconfig ' + svc
        with open(os.devnull, "w") as fnull:
            if not subprocess.call(cmd.split(), stdout=fnull, stderr=fnull):
                return ''
            else:
                return ' (disabled on boot)'

def service_status(svc):
    cmd = 'service ' + svc + ' status'
    cmdout = subprocess.Popen(cmd.split(), stdout=subprocess.PIPE).communicate()[0]
    if cmdout.find('running') != -1:
        return 'active'
    else:
        return 'inactive'

def check_svc(svc):
    psvc = svc + ':'
    if service_installed(svc):
        bootstatus = service_bootstatus(svc)
        status = service_status(svc)
    else:
        bootstatus = ' (disabled on boot)'
        status='inactive'
    print '%-30s%s%s' %(psvc, status, bootstatus)

_DEFAULT_CONF_FILE_DIR = '/etc/contrail/'
_DEFAULT_CONF_FILE_EXTENSION = '.conf'

def get_http_server_port_from_conf(svc_name, debug):
    # Open and extract conf file
    default_conf_file = _DEFAULT_CONF_FILE_DIR + svc_name + \
        _DEFAULT_CONF_FILE_EXTENSION
    try:
        fp = open(default_conf_file)
    except IOError as e:
        if debug:
            print '{0}: Could not read filename {1}'.format(\
                svc_name, default_conf_file)
        return -1
    else:
        data = StringIO('\n'.join(line.strip() for line in fp))
    # Parse conf file
    parser = ConfigParser.SafeConfigParser()
    try:
        parser.readfp(data)
    except ConfigParser.ParsingError as e:
        fp.close()
        if debug:
            print '{0}: Parsing error: {1}'.format(svc_name, \
                str(e))
        return -1
    # Read DEFAULT.http_server_port from the conf file. If that fails try
    # DEFAULTS.http_server_port (for python daemons)
    try:
        http_server_port = parser.getint('DEFAULT', 'http_server_port')
    except (ConfigParser.NoOptionError, ConfigParser.NoSectionError, \
            ValueError) as de:
        try:
            http_server_port = parser.getint('DEFAULTS', 'http_server_port')
        except (ConfigParser.NoOptionError, ConfigParser.NoSectionError) as dse:
            fp.close()
            if debug:
                print '{0}: DEFAULT/S.http_server_port not present'.format(
                    svc_name)
            return -1
        else:
            fp.close()
            return http_server_port
    else:
        fp.close()
        return http_server_port

def get_default_http_server_port(svc_name, debug):
    if svc_name in ServiceHttpPortMap:
        return ServiceHttpPortMap[svc_name]
    else:
        if debug:
            print '{0}: Introspect port not found'.format(svc_name)
        return -1

def get_http_server_port(svc_name, debug):
    http_server_port = get_http_server_port_from_conf(svc_name, debug)
    if http_server_port == -1:
        http_server_port = get_default_http_server_port(svc_name, debug)
    return http_server_port

def get_svc_uve_status(svc_name, debug):
    # Get the HTTP server (introspect) port for the service
    http_server_port = get_http_server_port(svc_name, debug)
    if http_server_port == -1:
        return None
    # Now check the NodeStatus UVE
    svc_introspect = IntrospectUtil('localhost', http_server_port, debug)
    node_status = svc_introspect.get_uve('NodeStatus')
    if node_status is None:
        if debug:
            print '{0}: NodeStatusUVE not found'.format(svc_name)
        return None
    if 'process_status' not in node_status:
        if debug:
            print '{0}: ProcessStatus not present in NodeStatusUVE'.format(
                svc_name)
        return None
    process_status_info = node_status['process_status']
    if len(process_status_info) == 0:
        if debug:
            print '{0}: Empty ProcessStatus in NodeStatusUVE'.format(svc_name)
        return None
    return process_status_info[0]['state']

def check_svc_status(service_name, debug):
    cmd = 'supervisorctl -s unix:///tmp/' + service_name.replace('-', 'd_') + '.sock' + ' status'
    cmdout = subprocess.Popen(cmd.split(), stdout=subprocess.PIPE).communicate()[0]
    if cmdout.find('refused connection') == -1:
        cmdout = cmdout.replace('   STARTING', 'initializing')
        cmdout = cmdout.replace('   RUNNING', 'active')
        cmdout = cmdout.replace('   STOPPED', 'inactive')
        cmdout = cmdout.replace('   FATAL', 'failed')
        cmdoutlist = cmdout.split('\n')
        if debug:
            print '%s: %s' % (str(service_name), cmdoutlist)
        for supervisor_svc_info_cmdout in cmdoutlist:
            supervisor_svc_info = supervisor_svc_info_cmdout.split()
            if len(supervisor_svc_info) >= 2:
                svc_name = supervisor_svc_info[0]
                svc_status = supervisor_svc_info[1]
                # Extract UVE state only for running processes
                if svc_name in NodeUVEImplementedServices and svc_status == 'active':
                    svc_uve_status = get_svc_uve_status(svc_name, debug)
                    if svc_uve_status is not None:
                        if svc_uve_status == 'Non-Functional':
                            svc_status = 'initializing'
                    else:
                        svc_status = 'initializing'
                print '{0:<30}{1:<20}'.format(svc_name, svc_status)
        print

def check_status(svc_name, debug):
    check_svc(svc_name)
    check_svc_status(svc_name, debug)

def supervisor_status(nodetype, debug):
    if nodetype == 'compute':
        print "== Contrail vRouter =="
        check_status('supervisor-vrouter', debug)
    elif nodetype == 'config':
        print "== Contrail Config =="
        check_status('supervisor-config', debug)
    elif nodetype == 'control':
        print "== Contrail Control =="
        check_status('supervisor-control', debug)
    elif nodetype == 'analytics':
        print "== Contrail Analytics =="
        check_status('supervisor-analytics', debug)
    elif nodetype == 'database':
        print "== Contrail Database =="
        check_status('supervisor-database', debug)
    elif nodetype == 'webui':
        print "== Contrail Web UI =="
        check_status('supervisor-webui', debug)

def package_installed(pkg):
    if distribution == 'debian':
        cmd = "dpkg -l " + pkg
    else:
        cmd = "rpm -q " + pkg
    with open(os.devnull, "w") as fnull:
        return (not subprocess.call(cmd.split(), stdout=fnull, stderr=fnull))

def main():
    parser = OptionParser()
    parser.add_option('-d', '--detail', dest='detail',
                      default=False, action='store_true',
                      help="show detailed status")
    parser.add_option('-x', '--debug', dest='debug',
                      default=False, action='store_true',
                      help="show contrail-status debugging information")
    
    (options, args) = parser.parse_args()
    if args:
        parser.error("No arguments are permitted")

    control = package_installed('contrail-control')
    analytics = package_installed('contrail-analytics')
    agent = package_installed('contrail-vrouter')
    capi = package_installed('contrail-config')
    cwebui = package_installed('contrail-web-core')
    database = package_installed('contrail-openstack-database')

    vr = False
    lsmodout = subprocess.Popen('lsmod', stdout=subprocess.PIPE).communicate()[0]
    if lsmodout.find('vrouter') != -1:
        vr = True

    if agent:
        if not vr:
            print "vRouter is NOT PRESENT\n"
        supervisor_status('compute', options.debug)
    else:
        if vr:
            print "vRouter is PRESENT\n"

    if control:
        supervisor_status('control', options.debug)

    if analytics:
        supervisor_status('analytics', options.debug)

    if capi:
        supervisor_status('config', options.debug)
    
    if cwebui:
        supervisor_status('webui', options.debug)

    if database:
        supervisor_status('database', options.debug)

    if len(glob.glob('/var/crashes/core.*')) != 0:
        print "========Run time service failures============="
        for file in glob.glob('/var/crashes/core.*'):
            print file

if __name__ == '__main__':
    main()

