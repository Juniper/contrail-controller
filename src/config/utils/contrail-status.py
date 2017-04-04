#! /usr/bin/python
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
from requests.packages.urllib3.exceptions import SubjectAltNameWarning
import warnings
warnings.filterwarnings('ignore', ".*SNIMissingWarning.*")
warnings.filterwarnings('ignore', ".*InsecurePlatformWarning.*")
warnings.filterwarnings('ignore', ".*SubjectAltNameWarning.*")
warnings.filterwarnings('ignore', category=SubjectAltNameWarning)
from StringIO import StringIO
from lxml import etree
from sandesh_common.vns.constants import ServiceHttpPortMap, \
    NodeUVEImplementedServices, ServicesDefaultConfigurationFiles, \
    BackupImplementedServices

DPDK_NETLINK_TCP_PORT = 20914

CONTRAIL_SERVICES = {'compute' : {'sysv' : ['supervisor-vrouter'],
                                  'upstart' : ['supervisor-vrouter'],
                                  'supervisor' : ['supervisor-vrouter'],
                                  'systemd' : ['contrail-vrouter-agent',
                                               'contrail-tor-agent',
                                               'contrail-vrouter-nodemgr']},
                     'control' : {'sysv' : ['supervisor-control'],
                                  'upstart' : ['supervisor-control'],
                                  'supervisor' : ['supervisor-control'],
                                  'systemd' :['contrail-control',
                                              'contrail-named',
                                              'contrail-dns',
                                              'contrail-control-nodemgr']},
                     'config' : {'sysv' : ['supervisor-config'],
                                 'upstart' : ['supervisor-config'],
                                 'supervisor' : ['supervisor-config'],
                                 'systemd' :['contrail-api',
                                             'contrail-schema',
                                             'contrail-svc-monitor',
                                             'contrail-device-manager',
                                             'contrail-config-nodemgr']},
                     'analytics' : {'sysv' : ['supervisor-analytics'],
                                    'upstart' : ['supervisor-analytics'],
                                    'supervisor' : ['supervisor-analytics'],
                                    'systemd' :['contrail-collector',
                                                'contrail-analytics-api',
                                                'contrail-query-engine',
                                                'contrail-alarm-gen',
                                                'contrail-snmp-collector',
                                                'contrail-topology',
                                                'contrail-analytics-nodemgr',]},
                     'database' : {'sysv' : ['supervisor-database'],
                                   'upstart' : ['supervisor-database'],
                                   'supervisor' : ['supervisor-database'],
                                  'systemd' :['kafka',
                                              'contrail-database-nodemgr']},
                     'webui' : {'sysv' : ['supervisor-webui'],
                                'upstart' : ['supervisor-webui'],
                                'supervisor' : ['supervisor-webui'],
                                'systemd' :['contrail-webui',
                                            'contrail-webui-middleware']},
                     'support-service' : {'sysv' : ['supervisor-support-service'],
                                          'upstart' : ['supervisor-support-service'],
                                          'supervisor' : ['supervisor-support-service'],
                                          'systemd' :['rabbitmq-server',
                                                      'zookeeper']},
                    }
distribution = platform.linux_distribution()[0].lower()
if distribution.startswith('centos') or \
   distribution.startswith('red hat'):
    distribution = 'redhat'
elif distribution.startswith('ubuntu'):
    distribution = 'debian'

def get_init_systems():
    init_sys_used = None
    try:
        with open(os.devnull, "w") as fnull:
            subprocess.check_call(["pidof", "systemd"], stdout=fnull,
                    stderr=fnull)
        init = 'systemd'
    except:
        try:
            with open(os.devnull, "w") as fnull:
                subprocess.check_call(["initctl", "list"], stdout=fnull,
                       stderr=fnull)
            init = 'upstart'
            # On docker initctl is redirected to /bin/true and so we need to use
            # sysv. Verify that some fake command also returns success to
            # determine.
            try:
                with open(os.devnull, "w") as fnull:
                    subprocess.check_call(["initctl", "fake"], stdout=fnull,
                            stderr=fnull)
                init = 'sysv'
                init_sys_used = 'supervisor'
            except:
                pass
        except:
            init = 'sysv'

    # contrail services in redhat system uses sysv, though systemd is default.
    if not init_sys_used:
        init_sys_used = init
    if distribution in ['redhat']:
        init_sys_used = 'sysv'
    return (init, init_sys_used)
# end get_init_systems

(init, init_sys_used) = get_init_systems()

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
    def __init__(self, ip, port, debug, timeout, keyfile, certfile, cacert):
        self._ip = ip
        self._port = port
        self._debug = debug
        self._timeout = timeout
        self._certfile = certfile
        self._keyfile = keyfile
        self._cacert = cacert
    #end __init__

    def _mk_url_str(self, path, secure=False):
        if secure:
            return "https://%s:%d/%s" % (self._ip, self._port, path)
        return "http://%s:%d/%s" % (self._ip, self._port, path)
    #end _mk_url_str

    def _load(self, path):
        url = self._mk_url_str(path)
        try:
            resp = requests.get(url, timeout=self._timeout)
        except requests.ConnectionError:
            url = self._mk_url_str(path, True)
            resp = requests.get(url, timeout=self._timeout, verify=\
                    self._cacert, cert=(self._certfile, self._keyfile))
        if resp.status_code == requests.codes.ok:
            return etree.fromstring(resp.text)
        else:
            if self._debug:
                print 'URL: %s : HTTP error: %s' % (url, str(resp.status_code))
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

def service_installed(svc, initd_svc):
    si_init = init
    if initd_svc:
        si_init = 'sysv'
    if distribution == 'redhat':
        cmd = 'chkconfig --list %s' % svc
    else:
        if si_init == 'systemd':
            cmd = 'systemctl cat %s' % svc
        elif si_init == 'upstart':
            cmd = 'initctl show-config %s' % svc
        else:
            return os.path.exists('/etc/init.d/%s' % svc)
    with open(os.devnull, "w") as fnull:
        return not subprocess.call(cmd.split(), stdout=fnull, stderr=fnull)
# end service_installed

def service_bootstatus(svc, initd_svc):
    sb_init = init
    if initd_svc:
        sb_init = 'sysv'
    if distribution == 'redhat':
        cmd = 'chkconfig %s' % svc
    else:
        if sb_init == 'systemd':
            cmd = 'systemctl is-enabled %s' % svc
        elif sb_init == 'upstart':
            cmd = 'initctl show-config %s' % svc
            cmdout = subprocess.Popen(cmd.split(), stdout=subprocess.PIPE).communicate()[0]
            if cmdout.find('  start on') != -1:
                return ''
            else:
                return ' (disabled on boot)'
        else:
            # On ubuntu/debian there does not seem to be an easy way to find
            # the boot status for init.d services without going through the
            # /etc/rcX.d level
            if glob.glob('/etc/rc*.d/S*%s' % svc):
                return ''
            else:
                return ' (disabled on boot)'

    with open(os.devnull, "w") as fnull:
        if not subprocess.call(cmd.split(), stdout=fnull, stderr=fnull):
            return ''
        else:
            return ' (disabled on boot)'
 # end service_bootstatus

def service_status(svc, check_return_code):
    cmd = 'service ' + svc + ' status'
    p = subprocess.Popen(cmd.split(), stdout=subprocess.PIPE)
    cmdout = p.communicate()[0]
    if check_return_code:
        if p.returncode == 0 or 'Active: active' in cmdout:
            return 'active'
        else:
            return 'inactive'
    if cmdout.find('running') != -1:
        return 'active'
    else:
        return 'inactive'
# end service_status

def check_svc(svc, initd_svc=False):
    psvc = svc + ':'
    if service_installed(svc, initd_svc):
        bootstatus = service_bootstatus(svc, initd_svc)
        status = service_status(svc, initd_svc)
    else:
        bootstatus = ' (disabled on boot)'
        status='inactive'
    print '%-30s%s%s' %(psvc, status, bootstatus)

def _get_http_server_port_from_conf(svc_name, conf_file, debug):
    try:
        fp = open(conf_file)
    except IOError as e:
        if debug:
            print '{0}: Could not read filename {1}'.format(\
                svc_name, conf_file)
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

_DEFAULT_CONF_FILE_DIR = '/etc/contrail/'
_DEFAULT_CONF_FILE_EXTENSION = '.conf'

def get_http_server_port_from_conf(svc_name, debug):
    # Open and extract conf file
    if svc_name in ServicesDefaultConfigurationFiles:
        default_conf_files = ServicesDefaultConfigurationFiles[svc_name]
    else:
        default_conf_files = [_DEFAULT_CONF_FILE_DIR + svc_name + \
            _DEFAULT_CONF_FILE_EXTENSION]
    for conf_file in default_conf_files:
        http_server_port = _get_http_server_port_from_conf(svc_name, conf_file,
                                                           debug)
        if http_server_port != -1:
            return http_server_port
    return -1

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

def get_svc_uve_status(svc_name, debug, timeout, keyfile, certfile, cacert):
    # Get the HTTP server (introspect) port for the service
    http_server_port = get_http_server_port(svc_name, debug)
    if http_server_port == -1:
        return None, None
    # Now check the NodeStatus UVE
    svc_introspect = IntrospectUtil('127.0.0.1', http_server_port, debug, \
                                    timeout, keyfile, certfile, cacert)
    node_status = svc_introspect.get_uve('NodeStatus')
    if node_status is None:
        if debug:
            print '{0}: NodeStatusUVE not found'.format(svc_name)
        return None, None
    node_status = [item for item in node_status if 'process_status' in item]
    if not len(node_status):
        if debug:
            print '{0}: ProcessStatus not present in NodeStatusUVE'.format(
                svc_name)
        return None, None
    process_status_info = node_status[0]['process_status']
    if len(process_status_info) == 0:
        if debug:
            print '{0}: Empty ProcessStatus in NodeStatusUVE'.format(svc_name)
        return None, None
    description = process_status_info[0]['description']
    for connection_info in process_status_info[0].get('connection_infos', []):
        if connection_info.get('type') == 'ToR':
            description = 'ToR:%s connection %s' % (connection_info['name'], connection_info['status'].lower())
    return process_status_info[0]['state'], description

def check_tor_agent_svc_status(svc_name, options):
    cmd = 'systemctl list-unit-files | grep *tor-agent*'
    # Expected output from this command as follows
    #  contrail-tor-agent-1.service               disabled
    #  contrail-tor-agent-2.service               disabled
    # From this output trying to extract the tor-agent-id to
    # identify the specific tor-agent-process name
    cmdout = subprocess.Popen(cmd.split(), stdout=subprocess.PIPE).communicate()[0]
    cmdoutlist = cmdout.split('\n')
    for tor_agent_service in cmdoutlist:
        if 'tor-agent' in tor_agent_service:
            tor_agent_service_info = tor_agent_service.split('.')
            for tor_agent_service_item in tor_agent_service_info:
                svc_name = 'contrail-tor-agent'
                tor_agent_service_name = tor_agent_service_item.split('-')
                if 'tor' in tor_agent_service_name:
                    svc_name = svc_name + '-' + tor_agent_service_name[3]
                    check_status(svc_name, options)

def check_svc_status(service_name, debug, detail, timeout, keyfile, certfile, cacert):
    service_sock = service_name.replace('-', '_')
    service_sock = service_sock.replace('supervisor_', 'supervisord_') + '.sock'
    if os.path.exists('/tmp/' + service_sock):
        cmd = 'supervisorctl -s unix:///tmp/' + service_sock + ' status'
    else:
        cmd = 'supervisorctl -s unix:///var/run/' + service_sock + ' status'
    cmdout = subprocess.Popen(cmd.split(), stdout=subprocess.PIPE).communicate()[0]
    if cmdout.find('refused connection') == -1:
        cmdout = cmdout.replace('   STARTING', 'initializing')
        cmdout = cmdout.replace('   RUNNING', 'active')
        cmdout = cmdout.replace('   STOPPED', 'inactive')
        cmdout = cmdout.replace('   FATAL', 'failed')
        cmdout = cmdout.replace('   STOPPING', 'failed')
        cmdout = cmdout.replace('   EXITED', 'failed')
        cmdout = cmdout.replace('   FATAL', 'failed')
        cmdout = cmdout.replace('   UNKNOWN', 'failed')
        cmdoutlist = cmdout.split('\n')
        if debug:
            print '%s: %s' % (str(service_name), cmdoutlist)
        for supervisor_svc_info_cmdout in cmdoutlist:
            supervisor_svc_info = supervisor_svc_info_cmdout.split()
            if len(supervisor_svc_info) >= 2:
                svc_name = supervisor_svc_info[0]
                svc_status = supervisor_svc_info[1]
                svc_detail_info = ' '.join(supervisor_svc_info[2:])
                # Extract UVE state only for running processes
                svc_uve_description = None
                if (svc_name in NodeUVEImplementedServices or
                    svc_name.rsplit('-', 1)[0] in NodeUVEImplementedServices) and svc_status == 'active':
                    try:
                        svc_uve_status, svc_uve_description = \
                        get_svc_uve_status(svc_name, debug, timeout, keyfile,\
                                certfile, cacert)
                    except requests.ConnectionError, e:
                        if debug:
                            print 'Socket Connection error : %s' % (str(e))
                        svc_uve_status = "connection-error"
                    except (requests.Timeout, socket.timeout) as te:
                        if debug:
                            print 'Timeout error : %s' % (str(te))
                        svc_uve_status = "connection-timeout"

                    if svc_uve_status is not None:
                        if svc_uve_status == 'Non-Functional':
                            svc_status = 'initializing'
                        elif svc_uve_status == 'connection-error':
                            if svc_name in BackupImplementedServices:
                                svc_status = 'backup'
                            else:
                                svc_status = 'initializing'
                        elif svc_uve_status == 'connection-timeout':
                            svc_status = 'timeout'
                    else:
                        svc_status = 'initializing'
                    if svc_uve_description is not None and svc_uve_description is not '':
                        svc_status = svc_status + ' (' + svc_uve_description + ')'

                if not detail:
                    print '{0:<30}{1:<20}'.format(svc_name, svc_status)
                else:
                    print '{0:<30}{1:<20}{2:<40}'.format(svc_name, svc_status, svc_detail_info)
        print

def check_status(svc_name, options):
    do_check_svc = True
    if init_sys_used in ['supervisor'] and svc_name.startswith('supervisor'):
        do_check_svc = False
    if do_check_svc:
        check_svc(svc_name)
    if init_sys_used not in ['systemd']:
        check_svc_status(svc_name, options.debug, options.detail, \
                options.timeout, options.keyfile, options.certfile, \
                options.cacert)

def contrail_service_status(nodetype, options):
    if nodetype == 'compute':
        print "== Contrail vRouter =="
        for svc_name in CONTRAIL_SERVICES[nodetype][init_sys_used]:
            if svc_name == 'contrail-tor-agent':
                check_tor_agent_svc_status(svc_name, options)
            else:
                check_status(svc_name, options)
    elif nodetype == 'config':
        print "== Contrail Config =="
        for svc_name in CONTRAIL_SERVICES[nodetype][init_sys_used]:
            check_status(svc_name, options)
    elif nodetype == 'control':
        print "== Contrail Control =="
        for svc_name in CONTRAIL_SERVICES[nodetype][init_sys_used]:
            check_status(svc_name, options)
    elif nodetype == 'analytics':
        print "== Contrail Analytics =="
        for svc_name in CONTRAIL_SERVICES[nodetype][init_sys_used]:
            check_status(svc_name, options)
    elif nodetype == 'database':
        print "== Contrail Database =="
        initd_svc = init == 'sysv' or init == 'upstart'
        check_svc('contrail-database', initd_svc=initd_svc)
        print ""
        for svc_name in CONTRAIL_SERVICES[nodetype][init_sys_used]:
            check_status(svc_name, options)
    elif nodetype == 'webui':
        print "== Contrail Web UI =="
        for svc_name in CONTRAIL_SERVICES[nodetype][init_sys_used]:
            check_status(svc_name, options)
    elif (nodetype == 'support-service' and distribution == 'debian'):
        print "== Contrail Support Services =="
        for svc_name in CONTRAIL_SERVICES[nodetype][init_sys_used]:
            check_status(svc_name, options)

def package_installed(pkg):
    if distribution == 'debian':
        cmd = "dpkg-query -W -f=${VERSION} " + pkg
        # Handling virtual package in ubuntu
        if pkg == 'contrail-vrouter':
            cmd = "dpkg -l " + pkg
    elif distribution == 'redhat':
        cmd = "rpm -q --qf %{V} " + pkg
    with open(os.devnull, "w") as fnull:
        try:
            out = subprocess.check_output(cmd.split(), stderr=fnull)
            return True if out else False
        except subprocess.CalledProcessError:
            return False

def main():
    parser = OptionParser()
    parser.add_option('-d', '--detail', dest='detail',
                      default=False, action='store_true',
                      help="show detailed status")
    parser.add_option('-x', '--debug', dest='debug',
                      default=False, action='store_true',
                      help="show debugging information")
    parser.add_option('-t', '--timeout', dest='timeout', type="float",
                      default=2,
                      help="timeout in seconds to use for HTTP requests to services")
    parser.add_option('-k', '--keyfile', dest='keyfile', type="string",
                      default="/etc/contrail/ssl/private/server-privkey.pem",
                      help="ssl key file to use for HTTP requests to services")
    parser.add_option('-c', '--certfile', dest='certfile', type="string",
                      default="/etc/contrail/ssl/certs/server.pem",
                      help="certificate file to use for HTTP requests to services")
    parser.add_option('-a', '--cacert', dest='cacert', type="string",
                      default="/etc/contrail/ssl/certs/ca-cert.pem",
                      help="ca-certificate file to use for HTTP requests to services")

    (options, args) = parser.parse_args()
    if args:
        parser.error("No arguments are permitted")

    control = package_installed('contrail-control')
    analytics = package_installed('contrail-analytics')
    agent = package_installed('contrail-vrouter')
    capi = package_installed('contrail-config')
    cwebui = package_installed('contrail-web-controller')
    cwebstorage = package_installed('contrail-web-storage')
    database = (package_installed('contrail-openstack-database') or
                package_installed('contrail-database'))
    storage = package_installed('contrail-storage')

    vr = False
    lsmodout = None
    lsofvrouter = None
    try:
        lsmodout = subprocess.Popen('lsmod', stdout=subprocess.PIPE).communicate()[0]
    except Exception as lsmode:
        if options.debug:
            print 'lsmod FAILED: {0}'.format(str(lsmode))
    try:
        lsofvrouter = (subprocess.Popen(['lsof', '-ni:{0}'.format(DPDK_NETLINK_TCP_PORT),
                   '-sTCP:LISTEN'], stdout=subprocess.PIPE).communicate()[0])
    except Exception as lsofe:
        if options.debug:
            print 'lsof -ni:{0} FAILED: {1}'.format(DPDK_NETLINK_TCP_PORT, str(lsofe))

    if lsmodout and lsmodout.find('vrouter') != -1:
        vr = True

    elif lsofvrouter:
        vr = True

    if agent:
        if not vr:
            print "vRouter is NOT PRESENT\n"
        contrail_service_status('compute', options)
    else:
        if vr:
            print "vRouter is PRESENT\n"

    if control:
        contrail_service_status('control', options)

    if analytics:
        contrail_service_status('analytics', options)

    if capi:
        contrail_service_status('config', options)

    if cwebui or cwebstorage:
        contrail_service_status('webui', options)

    if database:
        contrail_service_status('database', options)

    if capi:
        if init in ['systemd']:
            contrail_service_status('support-service', options)
        else:
            service_name = 'supervisor-support-service'
            service_sock = service_name.replace('-', '_')
            service_sock = service_sock.replace('supervisor_', 'supervisord_') + '.sock'
            service_sock = "/var/run/%s" % service_sock
            if os.path.exists(service_sock):
                contrail_service_status('support-service', options)

    if storage:
        print "== Contrail Storage =="
        check_svc('contrail-storage-stats')

    if len(glob.glob('/var/crashes/core.*')) != 0:
        print "========Run time service failures============="
        for file in glob.glob('/var/crashes/core.*'):
            print file

if __name__ == '__main__':
    main()

