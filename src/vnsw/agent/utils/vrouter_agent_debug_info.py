#
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
  vrouter:
    node1:
      ip: <ip-address>
      ssh_user: <user should be root>
      ssh_pwd: <password>
    node2:
    .
    .
    .
    noden:
  control:
    node1:
      ip: <ip-address>
      ssh_user: <user should be root>
      ssh_pwd: <password>
    node2:
    .
    .
    .
    noden:
  gcore_needed: <true/false>

sample_input.yaml
-----------------
provider_config:
  vrouter:
    node1:
      ip: 10.204.216.10
      ssh_user: root
      ssh_pwd: c0ntrail123
    node2:
      ip: 10.204.216.11
      ssh_user: root
      ssh_pwd: c0ntrail123
  control:
    node1:
      ip: 10.204.216.12
      ssh_user: root
      ssh_pwd: c0ntrail123
    node2:
      ip: 10.204.216.13
      ssh_user: root
      ssh_pwd: c0ntrail123
  gcore_needed: true
"""
from __future__ import print_function

from future import standard_library
standard_library.install_aliases()
from builtins import str
from builtins import range
from builtins import object
import subprocess
import time
import paramiko
import sys
import json
import yaml
import warnings
warnings.filterwarnings(action='ignore',module='.*paramiko.*')
from urllib.request import urlopen
from urllib.error import URLError, HTTPError
import xml.etree.ElementTree as ET

class Debug(object):
    _base_dir = None
    _compress_file = None
    def __init__(self, dir_name, sub_dirs, process_name, container_name,
                log_path, lib_path, cli, host, port, user, pw):
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
        self._sub_dirs = sub_dirs

        self._ssh_client = self.create_ssh_connection(self._host,
                                                self._user, self._pwd)
        if self._ssh_client is None:
            # Ssh connection to agent node failed. Hence terminate script.
            sys.exit()
    # end __init__

    @staticmethod
    def create_base_dir(name):
        Debug._base_dir = '/var/log/%s-%s'%(name,
                time.strftime("%Y%m%d-%H%M%S"))
        cmd = 'mkdir %s' %(Debug._base_dir)
        subprocess.call(cmd, shell=True)
    # end create_base_dir

    def create_sub_dirs(self):
        timestr = time.strftime("%Y%m%d-%H%M%S")
        self._dir_name = '%s-%s-%s'%(self._dir_name, self._host,
                                        timestr)
        self._tmp_dir = '/tmp/%s'%(self._dir_name)
        self._parent_dir = '%s/%s' %(Debug._base_dir, self._dir_name)
        cmd = 'mkdir %s' %(self._tmp_dir)
        ssh_stdin,ssh_stdout,ssh_stderr = self._ssh_client.exec_command(cmd)
        cmd = 'mkdir %s' %(self._parent_dir)
        subprocess.call(cmd, shell=True)
        # create sub-directories
        for item in self._sub_dirs:
            cmd = 'mkdir %s/%s' %(self._parent_dir, item)
            subprocess.call(cmd, shell=True)
    # end create_directories

    def copy_logs(self):
        print('\nTASK : copy vrouter-agent logs')
        # use ftp to copy logs from node where agent is running
        sftp_client = self._ssh_client.open_sftp()
        remote_dir = '/var/log/contrail/'
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
        cmd = 'docker logs %s' %self._container
        f.write(cmd)
        cmd_op = self.get_ssh_cmd_output(cmd)
        f.write(cmd_op)
        f.write(op_separator)
        cmd = '\n\ndocker images'
        f.write(cmd)
        cmd_op = self.get_ssh_cmd_output(cmd)
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
        cmd = 'contrail-status'
        cmd_op = self.get_ssh_cmd_output(cmd)
        f.write(cmd_op)
        f.close()
        print('\nCopying contrail-version logs : Success')
    # end copy_contrail_status

    def generate_gcore(self):
        print('\nTASK : generate gcore')
        cmd = 'docker exec %s gcore $(pidof %s)' %(self._container,
                self._process_name)
        ssh_stdin, ssh_stdout, ssh_stderr = self._ssh_client.exec_command(cmd)
        exit_status = ssh_stdout.channel.recv_exit_status()
        if not exit_status  == 0:
            print('\nGenerating %s gcore : Failed. Error %s'\
                    %(self._process_name, exit_status))
            return 0
        cmd = 'docker exec %s ls core.$(pidof %s)'\
                %(self._container, self._process_name)
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
        cmd = 'docker cp %s:%s %s'%(self._container, self._core_file_name,
                                    self._tmp_dir)
        ssh_stdin, ssh_stdout, ssh_stderr = self._ssh_client.exec_command(cmd)
        exit_status = ssh_stdout.channel.recv_exit_status()
        if not exit_status  == 0:
            print('\nCopying %s gcore : Failed. Error %s'%exit_status)
            return
        src_file = '%s/%s'%(self._tmp_dir, self._core_file_name)
        dest_file = '%s/gcore/%s'%(self._parent_dir, self._core_file_name)
        if self.do_ftp(src_file, dest_file):
            print('\nCopying gcore file : Success')
        else:
            print('\nCopying gcore file : Failed')
    # end copy_gcore

    def copy_libraries(self, lib_list):
        print('\nTASK : copy libraries')
        for lib_name in lib_list:
            cmd = 'docker exec %s echo $(readlink %s%s.so*)' \
                        %(self._container, self._lib_path, lib_name)
            ssh_stdin,ssh_stdout,ssh_stderr = self._ssh_client.exec_command(cmd)
            exit_status = ssh_stdout.channel.recv_exit_status()
            if not exit_status  == 0:
                print('\nCopying library %s  : Failed. Error %s'\
                        %(lib_name, exit_status))
                return
            lib_name = ssh_stdout.readline().rstrip('\n')
            cmd = 'docker cp %s:%s%s %s' %(self._container,
                                            self._lib_path,
                                            lib_name, self._tmp_dir)
            ssh_stdin,ssh_stdout,ssh_stderr = self._ssh_client.exec_command(cmd)
            # need some delay to be sure that lib gets copied
            # and then do ftp
            time.sleep(5)
            if not exit_status  == 0:
                print('\nCopying library %s  : Failed. Error %s'\
                        %(lib_name, exit_status))
                return
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
            # create file name for for each command
            tmp_str = cmd_list[i].split()
            file_name = '_'.join(tmp_str)
            file_path = self._parent_dir + '/introspect/' + file_name
            cmd = 'docker exec %s %s %s >> %s' %(self._container,
                        self._cli, cmd_list[i], file_path)
            print('Collecting output of command [%s %s]' %(self._cli, cmd_list[i]))
            cmd_op = self.get_ssh_cmd_output(cmd)
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
        remote_dir = '/var/log/contrail/'
        for filename in sftp_client.listdir(remote_dir):
            if '.log' not in filename:
                continue
            src_file = '%s%s'%(remote_dir, filename)
            dest_file = '%s/%s'%(self._parent_dir, filename)
            sftp_client.get(src_file, dest_file)
        sftp_client.close()
        print('\nCopying controller logs: Success')
    # end copy_control_node_logs

    def create_ssh_connection(self, ip, user, pw):
        try:
            client = paramiko.SSHClient()
            client.load_system_host_keys()
            client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
            client.connect(ip, username=user, password=pw)
            return client
        except Exception as e:
            print('\nError: ssh connection failed for ip %s: %s' %(ip,e))
            return None
    # end create_ssh_connection

    def get_ssh_cmd_output(self, cmd):
        ssh_stdin, ssh_stdout, ssh_stderr = self._ssh_client.exec_command(cmd)
        exit_status = ssh_stdout.channel.recv_exit_status()
        if not exit_status  == 0:
            return
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
        print("\nDumping the output of commands to files")
        commands = ['nh --list', 'vrouter --info', 'dropstats',
                         'dropstats -l 0', 'vif --list', 'mpls --dump',
                         'vxlan --dump', 'vrfstats --dump', 'vrmemstats',
                         'qosmap --dump-fc',
                         'qosmap --dump-qos', 'mirror --dump', 'virsh list',
                         'ip ad', 'ip r', 'flow -s']

        myCmd = 'mkdir /tmp/vrouter_logs'
        cmd_op = self.get_ssh_cmd_output(myCmd)

        for i in range(len(commands)):
            str = commands[i]
            if str == "dropstats" or str == "dropstats -l 0":
                self.run_command_interval_times(str,5,5) #command,interval,times
            elif str == "flow -s":
                self.run_command_interval_times(str,20,1) #command,interval,times
            else:
                str_file_name =  str.replace(' ','')
                myCmd = 'docker exec vrouter_vrouter-agent_1 /bin/sh -c "%s" \
                        >> /tmp/vrouter_logs/%s.txt'%(str,str_file_name)
                cmd_op = self.get_ssh_cmd_output(myCmd)
                source_path = '/tmp/vrouter_logs/%s.txt'%(str_file_name)
                dest_path = '%s/vrouter_logs/%s.txt'%(self._parent_dir,str_file_name)
                if not self.do_ftp(source_path,dest_path):
                     print('\nCopying file %s : Failed'%source_path)

        self.get_per_vrf_logs()
        self.get_virsh_individual_stats()
    # end get_vrouter_logs


    def get_per_vrf_logs(self):
        print("\nParsing through the vrfstats dump and getting logs per vrf")

        myCmd = 'docker exec vrouter_vrouter-agent_1 /bin/sh -c \
            "vrfstats --dump" >> /tmp/VRF_File1.txt'
        cmd_op = self.get_ssh_cmd_output(myCmd)

        source_path = '/tmp/VRF_File1.txt'
        dest_path = '/tmp/VRF_File2.txt'
        if not self.do_ftp(source_path,dest_path):
            print('\nCopying file %s : Failed'%source_path)

        with open('/tmp/VRF_File2.txt') as file:
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

                    myCmd = 'docker exec vrouter_vrouter-agent_1 /bin/sh -c \
                        "rt --dump %d --family %s" >> \
                        /tmp/vrouter_logs/VRF_%d_Family%s'%(var,cmd,var,cmd)
                    cmd_op = self.get_ssh_cmd_output(myCmd)
                    source_path = '/tmp/vrouter_logs/VRF_%d_Family%s'%(var,cmd)
                    dest_path = '%s/vrouter_logs/VRF_%d_Family%s' \
                        %(self._parent_dir,var,cmd)
                    if not self.do_ftp(source_path,dest_path):
                        print('\nCopying file %s : Failed'%source_path)

        myCmd = 'rm -rf /tmp/VRF_File1.txt'
        cmd_op = self.get_ssh_cmd_output(myCmd)

        myCmd = 'rm -rf /tmp/VRF_File2.txt'
        cmd_op = self.get_ssh_cmd_output(myCmd)
        subprocess.call(myCmd,shell=True)

    #end get_per_vrf_logs

    def get_virsh_individual_stats(self):
        print("\nParsing through the virsh list and getting logs per virsh")

        myCmd = 'docker exec vrouter_vrouter-agent_1 /bin/sh -c \
            "virsh list" >> /tmp/VIRSH_File1.txt'
        cmd_op = self.get_ssh_cmd_output(myCmd)

        source_path = '/tmp/VIRSH_File1.txt'
        dest_path = '/tmp/VIRSH_File2.txt'
        if not self.do_ftp(source_path,dest_path):
            print('\nCopying file %s : Failed'%source_path)

        with open('/tmp/VIRSH_File2.txt', 'r') as file:
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

                    myCmd = 'docker exec vrouter_vrouter-agent_1 /bin/sh -c \
                        "virsh %s %s" >> \
                        /tmp/vrouter_logs/virsh_%s_%s'%(cmd,var,cmd,var)
                    cmd_op = self.get_ssh_cmd_output(myCmd)
                    source_path = '/tmp/vrouter_logs/virsh_%s_%s'%(cmd,var)
                    dest_path = '%s/vrouter_logs/virsh_%s_%s' \
                        %(self._parent_dir,cmd,var)
                    if not self.do_ftp(source_path,dest_path):
                        print('\nCopying file %s : Failed'%source_path)

        myCmd = 'rm -rf /tmp/VIRSH_File1.txt'
        cmd_op = self.get_ssh_cmd_output(myCmd)

        myCmd = 'rm -rf /tmp/VIRSH_File2.txt'
        cmd_op = self.get_ssh_cmd_output(myCmd)
        subprocess.call(myCmd,shell=True)

        myCmd = 'rm -rf /tmp/vrouter_logs'
        subprocess.call(myCmd,shell=True)
    #end get_virsh_individual_stats

    def run_command_interval_times(self,cmd,interval,times):
        str_file_name =  cmd.replace(' ','')
        for i in range(times):
            file_num = i+1
            str_file_name =  cmd.replace(' ','')
            myCmd = 'timeout %ds docker exec vrouter_vrouter-agent_1 \
                     /bin/sh -c "%s" \
                    >> /tmp/vrouter_logs/%s_%d.txt'%(interval,cmd,str_file_name,file_num)
            cmd_op = self.get_ssh_cmd_output(myCmd)
            time.sleep(2)
            source_path = '/tmp/vrouter_logs/%s_%d.txt'%(str_file_name,file_num)
            dest_path = '%s/vrouter_logs/%s_%d.txt'%(self._parent_dir,str_file_name,file_num)
            if not self.do_ftp(source_path,dest_path):
                print('\nCopying file %s : Failed'%source_path)
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
        cmd = 'rm -rf %s' %self._tmp_dir
        ssh_stdin, ssh_stdout, ssh_stderr = self._ssh_client.exec_command(cmd)
    # end cleanup

    @staticmethod
    def delete_base_dir():
        # delete this directory as we have zipped it now
        cmd = 'rm -rf %s' %Debug._base_dir
        subprocess.call(cmd, shell=True)
    # end delete_base_dir

class Introspect(object):
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
            print('The server couldn\'t fulfill the request.')
            print('URL: ' + url)
            print('Error code: ', e.code)
            return 0
        except URLError as e:
            print('Failed to reach destination')
            print('URL: ' + url)
            print('Reason: ', e.reason)
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
    print(USAGE_TEXT)
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
        pw = data['provider_config']['vrouter'][item]['ssh_pwd']
        port = 8085
        print('\nCollecting vrouter-agent logs for node : %s' %host)
        obj = Debug(dir_name='vrouter',
                sub_dirs=sub_dirs,
                process_name='contrail-vrouter-agent',
                container_name='vrouter_vrouter-agent_1',
                log_path='/var/log/contrail/',
                lib_path='/usr/lib64/',
                cli='contrail-vrouter-agent-cli',
                host=host,
                port=port,
                user=user,
                pw=pw)
        obj.create_sub_dirs()
        obj.copy_logs()
        obj.copy_docker_logs()
        obj.copy_contrail_status()
        lib_list = ['libc', 'libstdc++']
        obj.copy_libraries(lib_list)
        obj.copy_introspect()
        obj.copy_sandesh_traces('Snh_SandeshTraceBufferListRequest')
        obj.get_vrouter_logs()
        if gcore and obj.generate_gcore():
            obj.copy_gcore()
        obj.delete_tmp_dir()
# end collect_vrouter_node_logs

def collect_control_node_logs(data):
    sub_dirs = ['logs']
    for item in data['provider_config']['control']:
        host = data['provider_config']['control'][item]['ip']
        user = data['provider_config']['control'][item]['ssh_user']
        pw = data['provider_config']['control'][item]['ssh_pwd']
        print('\nCollecting controller logs for control node : %s' %host)
        obj = Debug(dir_name='control',
                sub_dirs=sub_dirs,
                process_name=None,
                container_name=None,
                log_path='/var/log/contrail/',
                lib_path=None,
                cli=None,
                host=host,
                port=None,
                user=user,
                pw=pw)
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
        return
    name = 'vrouter-agent-debug-info'
    Debug.create_base_dir(name)
    try:
        if yaml_data['provider_config']['vrouter']:
            collect_vrouter_node_logs(yaml_data)
    except Exception as e:
        pass
    try:
        if yaml_data['provider_config']['control']:
            collect_control_node_logs(yaml_data)
    except Exception as e:
        pass
    Debug.compress_folder(name)
    Debug.delete_base_dir()
# end main

if __name__ == '__main__':
    main()

