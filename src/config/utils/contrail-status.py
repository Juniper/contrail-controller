#!/usr/bin/python
#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

from optparse import OptionParser
import subprocess
import os
import glob

try:
    subprocess.check_call(["dpkg-vendor", "--derives-from", "debian"])
    distribution = 'debian'
except:
    distribution = 'centos'

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

def check_supervisor_svc(server_port):
    cmd = 'supervisorctl -s http://localhost:' + str(server_port) + ' status'
    cmdout = subprocess.Popen(cmd.split(), stdout=subprocess.PIPE).communicate()[0]
    if cmdout.find('refused connection') == -1:
        cmdout = cmdout.replace('   RUNNING', 'active')
        cmdout = cmdout.replace('   STOPPED', 'inactive')
        cmdout = cmdout.replace('   FATAL', 'failed')
        cmdoutlist = cmdout.split('\n')
        for out in cmdoutlist:
            print out[0:40]

def supervisor_status(nodetype):
    if nodetype == 'compute':
        print "== Contrail vRouter =="
        check_svc('supervisor-vrouter')
        check_supervisor_svc(9005)
    elif nodetype == 'config':
        print "== Contrail Config =="
        check_svc('supervisor-config')
        check_supervisor_svc(9004)
    elif nodetype == 'control':
        print "== Contrail Control =="
        check_svc('supervisor-control')
        check_supervisor_svc(9003)
    elif nodetype == 'analytics':
        print "== Contrail Analytics =="
        check_svc('supervisor-analytics')
        check_supervisor_svc(9002)
    elif nodetype == 'database':
        print "== Contrail Database =="
        check_svc('supervisord-contrail-database')
        check_supervisor_svc(9007)
    elif nodetype == 'webui':
        print "== Contrail Web UI =="
        check_svc('supervisor-webui')
        check_supervisor_svc(9008)

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
        supervisor_status('compute')
    else:
        if vr:
            print "vRouter is PRESENT\n"

    if control:
        supervisor_status('control')

    if analytics:
        supervisor_status('analytics')

    if capi:
        supervisor_status('config')
    
    if cwebui:
        supervisor_status('webui')

    if database:
        supervisor_status('database')

    if len(glob.glob('/var/crashes/core.*')) != 0:
        print "========Run time service failures============="
        for file in glob.glob('/var/crashes/core.*'):
            print file

if __name__ == '__main__':
    main()

