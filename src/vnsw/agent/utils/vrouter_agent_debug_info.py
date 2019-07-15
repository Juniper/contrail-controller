
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
Synopsis:
    Collect logs, gcore(optional), introspect logs, sandesh traces and
    docker logs from vrouter node.
    Collect logs specific to control node.
Usage:
    python vrouter_agent_debug_info.py -i <input_file>
Options:
    -h, --help          Display this help information.
    -i                  Input file which has vrouter and control node details.
                        This file should be a yaml file.
                        Template and a sample input_file is shown below.
                        You need to mention ip, ssh_user and ssh_pwd
                        required to login to vrouter/control node.
                        You can add multiple vrouter or control node details.
                        Specify gcore_needed as true if you want to collect gcore.
Template for input file:
------------------------
provider_config:
  deployment_type: <'rhosp'/'openstack'/'kubernetes'> 
  vrouter:
    node_1
      ip: <ip-address>
      ssh_user: <username>
      ssh_pwd: <password mandatory for 'openstack' and 'kubernetes'>
      ssh_key_file: <key file madatory for 'rhosp'>
    node_2:
    .
    .
    .
    node_n:
  control:
    node_1
      ip: <ip-address>
      ssh_user: <username>
      ssh_pwd: <password mandatory for 'openstack' and 'kubernetes'>
      ssh_key_file: <key file madatory for 'rhosp'>
    node_2:
    .
    .
    .
    node_n:
  gcore_needed: <true/false>

sample_input.yaml
-----------------
provider_config:
  # deployment_type: 'rhosp'/'openstack'/'kubernetes'
  deployment_type: 'rhosp'
  vrouter:
    node1:
      ip: 192.168.24.7
      ssh_user: heat-admin
      # if deployment_type is rhosp then ssh_key_file is mandatory
      ssh_key_file: '/home/stack/.ssh/id_rsa'
      ssh_pwd : 'c0ntrail123'
    node2:
      ip: 192.168.24.8
      ssh_user: heat-admin
      # if deployment_type is rhosp then ssh_key_file is mandatory
      ssh_key_file: '/home/stack/.ssh/id_rsa'
  control:
    node1:
      ip: 192.168.24.6
      ssh_user: heat-admin
      ssh_key_file: '/home/stack/.ssh/id_rsa'
    node2:
      ip: 192.168.24.23
      ssh_user: heat-admin
      ssh_key_file: '/home/stack/.ssh/id_rsa'
  gcore_needed: true
"""

import subprocess
import time
import sys
import warnings
warnings.filterwarnings(action='ignore',module='.*paramiko.*')
from urllib2 import urlopen, URLError, HTTPError
import paramiko
import yaml
import xml.etree.ElementTree as ET

sudo_prefix = 'sudo '
deployment_map = {'rhosp': {'container_name':'contrail_vrouter_agent',
                        'lib_path': '/usr/lib64/',
                        'log_path':'/var/log/containers/contrail/'},
                'openstack': {'container_name': 'vrouter_vrouter-agent_1',
                        'lib_path': '/usr/lib64/',
                        'log_path':'/var/log/contrail/'},
                'kubernetes': {'container_name': 'vrouter_vrouter-agent_1',
                        'lib_path': '/usr/lib64/',
                        'log_path':'/var/log/contrail/'}}
class Debug(object):
    _base_dir = None
    _compress_file = None
    def __init__(self, dir_name, sub_dirs, process_name, container_name,
                log_path, lib_path, cli, host, port, user, pw, ssh_key_file):
        self._dir_name = dir_name
        self._tmp_dir = None
        self._process_name = process_name
        self._container = container_name
        self._log_path = log_path
        self._lib_path = lib_path
        self._cli = cli
        self._host = host
        self._port = port
        self._introspect = Introspect(host, port)
        self._user = user
        self._pwd = pw
        self._ssh_key_file = ssh_key_file
        self._sub_dirs = sub_dirs
        self._core_file_name = None
        self._parent_dir = None
        self._ssh_client = self.create_ssh_connection(self._host, self._user,
                                        self._pwd, self._ssh_key_file)
        if self._ssh_client is None:
            # Ssh connection to agent node failed. Hence terminate script.
            print('ssh client is None')
            sys.exit()
    # end __init__

    @staticmethod
    def create_base_dir(name):
        Debug._base_dir = '/var/log/%s-%s'%(name,
                time.strftime("%Y%m%d-%H%M%S"))
        cmd = sudo_prefix + 'mkdir %s' %(Debug._base_dir)
        subprocess.call(cmd, shell=True)
    # end create_base_dir

    def create_sub_dirs(self):
        timestr = time.strftime("%Y%m%d-%H%M%S")
        self._dir_name = '%s-%s-%s'%(self._dir_name, self._host,
                                        timestr)
        self._tmp_dir = '/tmp/%s'%(self._dir_name)
        self._parent_dir = '%s/%s' %(Debug._base_dir, self._dir_name)
        cmd = sudo_prefix + 'mkdir %s' %(self._tmp_dir)
        ssh_stdin,ssh_stdout,ssh_stderr = self._ssh_client.exec_command(cmd)
        cmd = sudo_prefix + 'mkdir %s' %(self._parent_dir)
        subprocess.call(cmd, shell=True)
        # create sub-directories
        for item in self._sub_dirs:
            cmd = sudo_prefix + 'mkdir %s/%s' %(self._parent_dir, item)
            subprocess.call(cmd, shell=True)
    # end create_directories

    def copy_logs(self):
        print('\nTASK : copy vrouter-agent logs')
        # use ftp to copy logs from node where agent is running
        sftp_client = self._ssh_client.open_sftp()
        remote_dir = self._log_path
        for filename in sftp_client.listdir(remote_dir):
            if '.log' not in filename:
                continue
            src_file = '%s%s'%(remote_dir, filename)
            dest_file = '%s/logs/%s'%(self._parent_dir, filename)
            sftp_client.get(src_file, dest_file)
        sftp_client.close()
        print('\nCopying vrouter-agent logs : Success')
    # end copy_logs

    def copy_docker_logs(self):
        print('\nTASK : copy docker logs')
        op_separator = '*'*100
        file_path = '%s/logs/docker_logs.txt' %(self._parent_dir)
        try:
            f = open(file_path, 'a')
        except Exception as e:
            print('\nError opening file %s: %s' %(file_path, e))
            print('\nCopying docker logs %s : Failed')
            return
        # run below commands in node where agent is running
        # and append the logs to file
        cmd = sudo_prefix + 'docker logs %s' %self._container
        if cmd:
            f.write(cmd)
        cmd_op = self.get_ssh_cmd_output(cmd)
        if cmd_op:
            f.write(cmd_op)
        f.write(op_separator)
        cmd = sudo_prefix + 'docker images'
        if cmd:
            f.write(cmd)
        cmd_op = self.get_ssh_cmd_output(cmd)
        if cmd_op:
            f.write(cmd_op)
        f.close()
        print('\nCopying docker logs : Success')
    # end copy_docker_logs

    def copy_contrail_status(self):
        print('\nTASK : copy contrail status')
        file_path = '%s/logs/contrail_version.txt' %(self._parent_dir)
        try:
            f = open(file_path, 'a')
        except Exception as e:
            print('\nError opening file %s: %s' %(file_path, e))
            print('\nCopying contrail-version logs : Failed')
            return
        # run 'contrail-status' on agent node and collect the logs
        cmd = sudo_prefix + 'contrail-status'
        cmd_op = self.get_ssh_cmd_output(cmd)
        if cmd_op:
            f.write(cmd_op)
        f.close()
        print('\nCopying contrail-version logs : Success')
    # end copy_contrail_status

    def generate_gcore(self):
        print('\nTASK : generate gcore')
        cmd = sudo_prefix + 'docker exec %s pidof %s' %(self._container,
                self._process_name)
        ssh_stdin, ssh_stdout, ssh_stderr = self._ssh_client.exec_command(cmd)
        pid = ssh_stdout.readline().strip()
        gcore_name = 'core.%s'%pid

        cmd = sudo_prefix + 'docker exec %s gcore %s' %(self._container, pid)
        ssh_stdin, ssh_stdout, ssh_stderr = self._ssh_client.exec_command(cmd)
        exit_status = ssh_stdout.channel.recv_exit_status()
        if not exit_status  == 0:
            print('\nGenerating %s gcore : Failed. Error %s'\
                    %(self._process_name, exit_status))
            return 0
        cmd = sudo_prefix + 'docker exec %s ls %s' %(self._container, gcore_name)
        ssh_stdin, ssh_stdout, ssh_stderr = self._ssh_client.exec_command(cmd)
        core_name = ssh_stdout.readline().strip('\n')
        if 'core' not in core_name:
            print('\nGenerating %s gcore : Failed. Gcore not found'\
                    %(self._process_name))
            return 0

        self._core_file_name = core_name
        print('\nGenerating %s gcore : Success' %(self._process_name))
        return 1
    # end generate_gcore

    def copy_gcore(self):
        print('\nTASK : copy gcore')
        shared_path = self.get_shared_dir_path()
        cmd = sudo_prefix + 'cp %s/%s %s'%(shared_path, self._core_file_name, self._tmp_dir)
        ssh_stdin, ssh_stdout, ssh_stderr = self._ssh_client.exec_command(cmd)
        exit_status = ssh_stdout.channel.recv_exit_status()
        if not exit_status  == 0:
            print('\nCopying gcore failed')
            return
        src_file = '%s/%s'%(self._tmp_dir, self._core_file_name)
        dest_file = '%s/gcore/%s'%(self._parent_dir, self._core_file_name)
        if self.do_ftp(src_file, dest_file):
            print('\nCopying gcore file : Success')
            cmd = sudo_prefix + 'rm -rf %s/%s'%(shared_path, self._core_file_name)
            ssh_stdin, ssh_stdout, ssh_stderr = self._ssh_client.exec_command(cmd)
        else:
            print('\nCopying gcore file : Failed')
    # end copy_gcore

    def get_shared_dir_path(self):
        cmd = sudo_prefix + 'docker inspect %s | grep merge'%self._container
        ssh_stdin, ssh_stdout, ssh_stderr = self._ssh_client.exec_command(cmd)
        shared_path = ssh_stdout.readline()
        shared_path = shared_path.split(':')
        shared_path = shared_path[1]
        shared_path = shared_path[2:-3]
        return shared_path

    def copy_libraries(self, lib_list):
        print('\nTASK : copy libraries')
        shared_path = self.get_shared_dir_path()
        for lib_name in lib_list:
            cmd = sudo_prefix + 'docker exec %s echo $(readlink %s%s.so*)' \
                        %(self._container, self._lib_path, lib_name)
            # get exact library name
            ssh_stdin,ssh_stdout,ssh_stderr = self._ssh_client.exec_command(cmd)
            exit_status = ssh_stdout.channel.recv_exit_status()
            if not exit_status  == 0:
                print('\nCopying library %s  : Failed. Error %s'\
                        %(lib_name, exit_status))
                continue
            lib_name = ssh_stdout.readline().rstrip('\n')
            # copy library to tmp directory
            src_file = '%s/usr/lib64/%s'%(shared_path, lib_name)
            cmd = sudo_prefix + 'cp %s %s'%(src_file, self._tmp_dir)
            ssh_stdin,ssh_stdout,ssh_stderr = self._ssh_client.exec_command(cmd)
            exit_status = ssh_stdout.channel.recv_exit_status()
            if not exit_status  == 0:
                print('\nCopying library %s  : Failed. Error %s'\
                        %(lib_name, exit_status))
                continue
            # do ftp to copy the library
            src_file = '%s/%s'%(self._tmp_dir, lib_name)
            dest_file = '%s/libraries/%s'%(self._parent_dir, lib_name)
            if self.do_ftp(src_file, dest_file):
                print('\nCopying library %s : Success' %lib_name)
            else:
                print('\nCopying library %s : Failed' %lib_name)
    # end copy_libraries

    def copy_introspect(self):
        print('\nTASK : Copy instrospect logs')
        cmd = 'docker exec %s %s read' %(self._container, self._cli)
        cmd = sudo_prefix + cmd
        # executing above command will return list of all
        # the sub-commands which we will run in loop to
        # collect all the agent introspect logs
        cmd_list = self.get_ssh_cmd_output(cmd).split('\n')
        # delete first and last element as they are not commands
        del cmd_list[0]
        if len(cmd_list) > 0:
            del cmd_list[-1]
        if not cmd_list:
            print('\nError running cmd: %s' %cmd)
            print('Copying introspect logs : Failed')
            return
        for i in range(len(cmd_list)):
            # remove leading and trailing white spaces if any
            cmd_list[i] = cmd_list[i].strip()
            cmd = sudo_prefix + 'docker exec %s %s %s' %(self._container,
                        self._cli, cmd_list[i])
            print('Collecting output of command [%s %s]' %(self._cli, cmd_list[i]))
            cmd_op = self.get_ssh_cmd_output(cmd)
            if not cmd_op:
                continue
            # create file name for for each command
            tmp_str = cmd_list[i].split()
            file_name = '_'.join(tmp_str)
            file_path = self._parent_dir + '/introspect/' + file_name
            try:
                f = open(file_path, 'a')
            except Exception as e:
                print('\nError opening file %s: %s' %(file_path, e))
                continue
            f.write(cmd_op)
            f.close()
        print('\nCopying introspect logs : Success')
    # end copy_introspect

    def copy_sandesh_traces(self, path):
        print('\nTASK : copy sandesh traces')
        if self._introspect.get(path) == 0:
            print('\nCopying sandesh traces : Failed')
            return
        trace_buffer_list =self._introspect.getTraceBufferList('trace_buf_name')
        for i in range(len(trace_buffer_list)):
            tmp_str = trace_buffer_list[i].split()
            file_name = '_'.join(tmp_str)
            file_path = self._parent_dir + '/sandesh_trace/' + file_name
            try:
                f = open(file_path, 'a')
            except Exception as e:
                print('\nError opening file %s: %s' %(file_path, e))
                print('\nCopying sandesh traces : Failed')
                return
            print('Collecting sandesh trace [%s]' %trace_buffer_list[i])
            self._introspect.get('Snh_SandeshTraceRequest?x=' \
                    + trace_buffer_list[i])
            if self._introspect.output_etree is not None:
                for element in \
                    self._introspect.output_etree.iter('element'):
                    f.write( \
                        Introspect.elementToStr('', element).rstrip())
                    f.write('\n')
            f.close()
        print('\nCopying sandesh traces : Success')
    # end copy_sandesh_traces

    def copy_control_node_logs(self):
        print('\nTASK : controller logs')
        if self._ssh_client is None:
            print('\nCopying controller logs: Failed')
            return
        # open ftp connection and copy logs
        sftp_client = self._ssh_client.open_sftp()
        remote_dir = self._log_path
        for filename in sftp_client.listdir(remote_dir):
            if '.log' not in filename:
                continue
            src_file = '%s%s'%(remote_dir, filename)
            dest_file = '%s/%s'%(self._parent_dir, filename)
            sftp_client.get(src_file, dest_file)
        sftp_client.close()
        print('\nCopying controller logs: Success')
    # end copy_control_node_logs

    def create_ssh_connection(self, ip, user, pw, key_file):
        try:
            client = paramiko.SSHClient()
            client.load_system_host_keys()
            client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
            if key_file:
                client.connect(ip, username=user, key_filename=key_file)
            else:
                client.connect(ip, username=user, password=pw)
            return client
        except Exception as e:
            print('\nError: ssh connection failed for ip %s: %s' %(ip,e))
            return None
    # end create_ssh_connection

    def get_ssh_cmd_output(self, cmd):
        ssh_stdin, ssh_stdout, ssh_stderr = self._ssh_client.exec_command(cmd)
        output = ""
        stdout=ssh_stdout.readlines()
        for line in stdout:
            output=output+line
        if output!="":
            return output
        else:
            return "There was no output for this command"
    # end get_ssh_cmd_output

    def do_ftp(self, src_file, dest_file):
        sftp_client = self._ssh_client.open_sftp()
        max_count = 10
        count = 0
        while count < max_count:
            count = count + 1
            try:
                sftp_client.get(src_file, dest_file)
            except Exception as e:
                print('\n%s while copying file %s. Retry attempt %s of %s '\
                        %(e, src_file, count, max_count))
                time.sleep(5)
                continue
            else:
                sftp_client.close()
                return 1
        sftp_client.close()
        return 0
    # end do_ftp

    def get_vrouter_logs(self):
        print('\nTASK : copy vrouter logs')
        print "\nDumping the output of commands to files"
        commands = ['nh --list', 'vrouter --info', 'dropstats',
                         'dropstats -l 0', 'vif --list', 'mpls --dump',
                         'vxlan --dump', 'vrfstats --dump', 'vrmemstats',
                         'qosmap --dump-fc',
                         'qosmap --dump-qos', 'mirror --dump', 'virsh list',
                         'ip ad', 'ip r', 'flow -s']

        myCmd = sudo_prefix + 'mkdir /tmp/vrouter_logs'
        cmd_op = self.get_ssh_cmd_output(myCmd)

        for i in range(len(commands)):
            str_value = commands[i]
            if str_value == "dropstats" or str_value == "dropstats -l 0":
                self.run_command_interval_times(str_value,5,5) #command,interval,times
            elif str_value == "flow -s":
                self.run_command_interval_times(str_value,20,1) #command,interval,times
            else:
                str_file_name =  str_value.replace(' ','')
                myCmd = sudo_prefix + 'docker exec %s /bin/sh -c "%s" '%(self._container, str_value)
                cmd_op = self.get_ssh_cmd_output(myCmd)
                if not cmd_op:
                    continue
                # create file name for for each command
                file_path = self._parent_dir + '/vrouter_logs/' + str_file_name
                try:
                    f = open(file_path, 'a')
                except Exception as e:
                    print('\nError opening file %s: %s' %(file_path, e))
                    continue
                f.write(cmd_op)
                f.close()
        try:
            self.get_per_vrf_logs()
        except Exception as e:
            print('Got exception %s' %e)
        try:
            self.get_virsh_individual_stats()
        except Exception as e:
            print('Got exception %s' %e)
    # end get_vrouter_logs

    def get_per_vrf_logs(self):
        print "\nParsing through the vrfstats dump and getting logs per vrf"
        myCmd = sudo_prefix + 'docker exec %s /bin/sh -c \
                "vrfstats --dump"'%(self._container)
        cmd_op = self.get_ssh_cmd_output(myCmd)

        if not cmd_op:
           print('No output for command %s' %myCmd)
           return
        #create temp file
        file_name = "VRF_File2.txt"
        file_path = self._parent_dir + '/' + file_name
        try:
            f = open(file_path, 'a')
        except Exception as e:
            print('\nError opening file %s: %s' %(file_path, e))
            return
        f.write(cmd_op)
        f.close()

        with open(file_path) as file:
            data = file.read()
            values = []
            index = 0
            index2 = 0

        while index < len(data):
            index = data.find('Vrf:', index)
            if index == -1:
                break

            value = int(data[index+5])
            index2 = data.find('\n', index)

            value2 = data[index+5:index2]
            value3 = int(value2)

            if value3!=None :
                values.append(value3)

            index += 4
            index2 += 4

        family = ['inet', 'inet6', 'bridge']

        for i in range(len(values)):
            if values[i] != None :
                var = (values[i])

                for i in range(len(family)):
                    cmd = family[i]

                    myCmd = sudo_prefix + 'docker exec %s /bin/sh -c \
                        "rt --dump %d --family %s" >>'%(self._container,var,cmd)
                    cmd_op = self.get_ssh_cmd_output(myCmd)
                    dest_path = '%s/vrouter_logs/VRF_%d_Family%s' \
                        %(self._parent_dir,var,cmd)
                    #create file
                    try:
                        f = open(dest_path, 'a')
                    except Exception as e:
                        print('\nError opening file %s: %s' %(dest_path, e))
                        continue
                    if not cmd_op:
                        continue
                    f.write(cmd_op)
                    f.close()

        file.close()
        myCmd = sudo_prefix + 'rm -rf %s'%file_path
        cmd_op = self.get_ssh_cmd_output(myCmd)
        subprocess.call(myCmd,shell=True)

    #end get_per_vrf_logs

    def get_virsh_individual_stats(self):
        print "\nParsing through the virsh list and getting logs per virsh"

        myCmd = sudo_prefix + 'docker exec %s /bin/sh -c \
            "virsh list"'%(self._container)
        cmd_op = self.get_ssh_cmd_output(myCmd)

        if not cmd_op:
            print('No output for the command [%s]'%myCmd)
            return
        #create temp file
        file_name = "VIRSH_File2.txt"
        file_path = self._parent_dir + '/' + file_name
        try:
            f = open(file_path, 'a')
        except Exception as e:
            print('\nError opening file %s: %s' %(file_path, e))
            return
        f.write(cmd_op)
        f.close()

        with open(file_path, 'r') as file:
            data = file.read()
            values = []
            index = 0
            index2 = 0

        while index < len(data):
            index = data.find('instance-', index)
            if index == -1:
                break

            value = data[index]
            index2 = data.find(' ', index)

            value2 = data[index:index2]
            value3 = str(value2)

            if value3!=None :
                values.append(value3)

            index += 9
            index2 += 9

        commands = ['domstats', 'domiflist']

        for i in range(len(values)):
            if values[i] != None :
                var = (values[i])

                for i in range(len(commands)):
                    cmd = commands[i]

                    myCmd = sudo_prefix + 'docker exec %s /bin/sh -c \
                        "virsh %s %s"'%(self._container,cmd,var)
                    cmd_op = self.get_ssh_cmd_output(myCmd)
                    dest_path = '%s/vrouter_logs/virsh_%s_%s' \
                        %(self._parent_dir,cmd,var)

                    #create file
                    try:
                        f = open(dest_path, 'a')
                    except Exception as e:
                        print('\nError opening file %s: %s' %(dest_path, e))
                        continue
                    f.write(cmd_op)
                    f.close()

        file.close()
        myCmd = sudo_prefix + 'rm -rf %s'%file_path
        cmd_op = self.get_ssh_cmd_output(myCmd)
        subprocess.call(myCmd,shell=True)

    #end get_virsh_individual_stats

    def run_command_interval_times(self,cmd,interval,times):
        for i in range(times):
            file_num = i+1
            str_file_name =  cmd.replace(' ','')
            myCmd = sudo_prefix + 'timeout %ds docker exec %s /bin/sh -c "%s"'\
                        %(interval, self._container, cmd)
            cmd_op = self.get_ssh_cmd_output(myCmd)
            time.sleep(2)
            if not cmd_op:
                print('No output for the command %s'%myCmd)
                return
            # create file name
            file_path = self._parent_dir + '/vrouter_logs/' + str_file_name + str(file_num)
            try:
                f = open(file_path, 'a')
            except Exception as e:
                print('\nError opening file %s: %s' %(file_path, e))
                return
            f.write(cmd_op)
            f.close()
    #end run_command_interval_times

    @staticmethod
    def compress_folder(name):
        print("\nCompressing folder %s" %Debug._base_dir)
        Debug._compress_file =  '/var/log/%s-%s.tar.gz'\
                %(name, time.strftime("%Y%m%d-%H%M%S"))
        cmd = 'tar -zcf %s %s > /dev/null 2>&1' \
                %(Debug._compress_file, Debug._base_dir)
        subprocess.call(cmd, shell=True)
        print('\nComplete logs copied at %s' %Debug._compress_file)
    #end compress_folder

    def delete_tmp_dir(self):
        # delete tmp directory
        myCmd = sudo_prefix + 'rm -rf %s' %self._tmp_dir
        ssh_stdin, ssh_stdout, ssh_stderr = self._ssh_client.exec_command(myCmd)
    # end cleanup

    @staticmethod
    def delete_base_dir():
        # delete this directory as we have zipped it now
        myCmd = sudo_prefix + 'rm -rf %s' %Debug._base_dir
        subprocess.call(myCmd, shell=True)
    # end delete_base_dir

class Introspect:
    def __init__ (self, host, port):
        self.host_url = "http://" + host + ":" + str(port) + "/"
    # end __init__

    def get (self, path):
        """ get introspect output """
        self.output_etree = None
        url = self.host_url + path.replace(' ', '%20')
        try:
          response = urlopen(url)
        except HTTPError as e:
            print 'The server couldn\'t fulfill the request.'
            print 'URL: ' + url
            print 'Error code: ', e.code
            return 0
        except URLError as e:
            print 'Failed to reach destination'
            print 'URL: ' + url
            print 'Reason: ', e.reason
            return 0
        else:
            ISOutput = response.read()
            response.close()
        self.output_etree = ET.fromstring(ISOutput)
        return 1
    # end get

    def getTraceBufferList(self, xpathExpr):
        """ get trace buffer list which contains all the trace names """
        trace_buffer_list = []
        if self.output_etree is not None:
            for element in self.output_etree.iter(xpathExpr):
                elem = Introspect.elementToStr('', element).rstrip()
                res = elem.split(':')[1].strip()
                trace_buffer_list.append(res)
        return trace_buffer_list
    # end getTraceBufferList

    @staticmethod
    def elementToStr(indent, etreenode):
        """ convernt etreenode sub-tree into string """
        elementStr=''
        if etreenode.tag == 'more':   #skip more element
            return elementStr

        if etreenode.text and etreenode.tag == 'element':
            return indent + etreenode.text + "\n"
        elif etreenode.text:
            return indent + etreenode.tag + ': ' + \
                    etreenode.text.replace('\n', '\n' + \
                    indent + (len(etreenode.tag)+2)*' ') + "\n"
        elif etreenode.tag != 'list':
            elementStr += indent + etreenode.tag + "\n"

        if 'type' in etreenode.attrib:
            if etreenode.attrib['type'] == 'list' and \
                    etreenode[0].attrib['size'] == '0':
                return elementStr

        for element in etreenode:
            elementStr += Introspect.elementToStr(indent + '  ', element)

        return elementStr
    # end elementToStr

USAGE_TEXT = __doc__

def usage():
    print USAGE_TEXT
    sys.exit(1)
# end usage

def parse_yaml_file(file_path):
    with open(file_path) as stream:
        try:
            yaml_data = yaml.safe_load(stream)
        except yaml.YAMLError as exc:
            print('Error[%s] while parsing file %s' %(exc, file_path))
            return None
        else:
            return yaml_data
# end parse_yaml_file

def get_deployment_type(data):
    try:
        deployment_type =  data['provider_config'].get('deployment_type')
    except Exception as e:
        print('Error[%s] parsing deployment_type'%e)
        sys.exit()
    else:
        if deployment_type is None:
        # openstack is the default deployment type
            deployment_type = 'openstack'
    return deployment_type
# end get_deployment_type

def collect_vrouter_node_logs(data):
    try:
        gcore = data['provider_config']['gcore_needed']
    except Exception as e:
         gcore = False
    sub_dirs = ['logs', 'gcore', 'libraries', 'introspect',
                    'sandesh_trace', 'vrouter_logs']
    for item in data['provider_config']['vrouter']:
        host = data['provider_config']['vrouter'][item]['ip']
        user = data['provider_config']['vrouter'][item]['ssh_user']
        deployment_type = get_deployment_type(data)
        ssh_key_file = None
        if deployment_type == 'rhosp':
            ssh_key_file = data['provider_config']['vrouter'][item]['ssh_key_file']
        pw = data['provider_config']['vrouter'][item].get('ssh_pwd')
        port = 8085
        print('\nCollecting vrouter-agent logs for node : %s' %host)
        obj = Debug(dir_name='vrouter',
                sub_dirs=sub_dirs,
                process_name='contrail-vrouter-agent',
                container_name=deployment_map[deployment_type]['container_name'],
                log_path=deployment_map[deployment_type]['log_path'],
                lib_path=deployment_map[deployment_type]['lib_path'],
                cli='contrail-vrouter-agent-cli',
                host=host,
                port=port,
                user=user,
                pw=pw,
                ssh_key_file = ssh_key_file)
        obj.create_sub_dirs()
        try:
            obj.copy_logs()
        except Exception as e:
            print('Error [%s] collecting agent logs'%e)
        try:
            obj.copy_docker_logs()
        except Exception as e:
            print('Error [%s] collecting docker logs'%e)
        try:
            obj.copy_contrail_status()
        except Exception as e:
            print('Error [%s] collecting contrail-status logs'%e)
        lib_list = ['libc', 'libstdc++']
        try:
            obj.copy_libraries(lib_list)
        except Exception as e:
            print('Error [%s] collecting libraries'%e)
        try:
            obj.copy_introspect()
        except Exception as e:
            print('Error [%s] collecting introspect'%e)
        try:
            obj.copy_sandesh_traces('Snh_SandeshTraceBufferListRequest')
        except Exception as e:
            print('Error [%s] collecting sandesh traces'%e)
        try:
            obj.get_vrouter_logs()
        except Exception as e:
            print('Error [%s] collecting vrouter logs'%e)
        try:
            if gcore and obj.generate_gcore():
                obj.copy_gcore()
        except Exception as e:
            print('Error [%s] collecting gcore'%e)
        obj.delete_tmp_dir()
# end collect_vrouter_node_logs

def collect_control_node_logs(data):
    sub_dirs = ['logs']
    for item in data['provider_config']['control']:
        host = data['provider_config']['control'][item]['ip']
        user = data['provider_config']['control'][item]['ssh_user']
        deployment_type = get_deployment_type(data)
        ssh_key_file = None
        if deployment_type == 'rhosp':
           ssh_key_file = data['provider_config']['control'][item]['ssh_key_file']
        pw = data['provider_config']['vrouter'][item].get('ssh_pwd')
        print('\nCollecting controller logs for control node : %s' %host)
        obj = Debug(dir_name='control',
                sub_dirs=sub_dirs,
                process_name=None,
                container_name=None,
                log_path=deployment_map[deployment_type]['log_path'],
                lib_path=None,
                cli=None,
                host=host,
                port=None,
                user=user,
                pw=pw,
                ssh_key_file = ssh_key_file)
        obj.create_sub_dirs()
        obj.copy_control_node_logs()
        obj.delete_tmp_dir()
# end collect_control_node_logs

def main():
    argv = sys.argv[1:]
    try:
        input_file = argv[argv.index('-i') + 1]
    except ValueError:
        usage()
        return
    yaml_data = parse_yaml_file(input_file)
    if yaml_data is None:
        print('Error parsing yaml file. Exiting!!!')
        return
    name = 'vrouter-agent-debug-info'
    Debug.create_base_dir(name)
    vrouter_error = control_error = False
    try:
        if yaml_data['provider_config']['vrouter']:
            collect_vrouter_node_logs(yaml_data)
    except Exception as e:
        print('Error [%s] while collecting vrouter logs'%e)
        vrouter_error = True
    try:
        if yaml_data['provider_config']['control']:
            collect_control_node_logs(yaml_data)
    except Exception as e:
        print('Error [%s] while collecting control node logs'%e)
        control_error = True
    if vrouter_error and control_error:
        print('No logs collected for vrouter and control node. Exiting!!!')
        return
    Debug.compress_folder(name)
    Debug.delete_base_dir()
# end main

if __name__ == '__main__':
    main()
