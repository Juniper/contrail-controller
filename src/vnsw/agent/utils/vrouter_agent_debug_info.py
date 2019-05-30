#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

""" python vrouter_agent_debug_info.py --host <ip>
           --port <port> --user <user> --pw <password> -file <path> -c """

import subprocess
import time
import paramiko
import sys
import json
import yaml
import warnings
warnings.filterwarnings(action='ignore',module='.*paramiko.*')
from urllib2 import urlopen, URLError, HTTPError
import xml.etree.ElementTree as ET

class Debug(object):
    def __init__(self, dir_name, process_name, container_name,
                log_path, lib_path, cli, config_file, host, port, user, pw):
        self._dir_name = dir_name
        self._parent_dir = None
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

        if self._user is None and self._pwd is None:
            # populate user and password from instance.yaml
            with open(config_file) as stream:
                try:
                    data = yaml.safe_load(stream)
                except yaml.YAMLError as exc:
                    print(exc)
            if data is not None :
                self._user = data['provider_config']['bms']['ssh_user']
                self._pwd = data['provider_config']['bms']['ssh_pwd']

        self._ssh_client = self.create_ssh_connection(self._host,
                                                self._user, self._pwd)
        if self._ssh_client is None:
            # Ssh connection to agent node failed. Hence terminate script.
            sys.exit()
    # end __init__

    def create_directories(self):
        sub_dirs = ['logs', 'gcore', 'libraries', 'introspect',
                        'sandesh_trace', 'controller', 'vrouter_logs']
        timestr = time.strftime("%Y%m%d-%H%M%S")
        self._dir_name = '%s-%s-%s'%(self._dir_name, self._host,
                                        timestr)
        self._tmp_dir = '/tmp/%s'%(self._dir_name)
        self._parent_dir = '/var/log/%s' %(self._dir_name)
        cmd = 'mkdir %s' %(self._tmp_dir)
        ssh_stdin,ssh_stdout,ssh_stderr = self._ssh_client.exec_command(cmd)
        cmd = 'mkdir %s' %(self._parent_dir)
        subprocess.call(cmd, shell=True)
        # create sub-directories
        for i in range(len(sub_dirs)):
            cmd = 'mkdir %s/%s' %(self._parent_dir, sub_dirs[i])
            subprocess.call(cmd, shell=True)
    # end create_directories

    def copy_logs(self):
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
        cmd = 'docker exec %s gcore $(pidof %s)' %(self._container,
                self._process_name)
        ssh_stdin, ssh_stdout, ssh_stderr = self._ssh_client.exec_command(cmd)
        print('\nGenerating %s gcore : Success' %(self._process_name))
    # end generate_gcore

    def copy_gcore(self):
        # since pid is appended to the core file,
        # we need to find pid of process
        cmd = 'echo $(pidof %s)'%self._process_name
        ssh_stdin, ssh_stdout, ssh_stderr = self._ssh_client.exec_command(cmd)
        proc_id = ssh_stdout.readline()
        proc_id = proc_id.rstrip('\n')
        # append pid to get file name
        core_file_name = 'core.%s'%proc_id
        cmd = 'docker cp %s:%s %s'%(self._container, core_file_name,
                                    self._tmp_dir)
        ssh_stdin, ssh_stdout, ssh_stderr = self._ssh_client.exec_command(cmd)
        time.sleep(10)
        src_file = '%s/%s'%(self._tmp_dir, core_file_name)
        dest_file = '%s/gcore/%s'%(self._parent_dir, core_file_name)
        self.do_ftp(src_file, dest_file)
        print('\nCopying gcore file : Success')
    # end copy_gcore

    def copy_libraries(self, lib_list):
        for lib_name in lib_list:
            cmd = 'docker exec %s echo $(readlink %s%s.so*)' \
                        %(self._container, self._lib_path, lib_name)
            ssh_stdin,ssh_stdout,ssh_stderr = self._ssh_client.exec_command(cmd)
            lib_name = ssh_stdout.readline().rstrip('\n')
            cmd = 'docker cp %s:%s%s %s' %(self._container,
                                            self._lib_path,
                                            lib_name, self._tmp_dir)
            ssh_stdin,ssh_stdout,ssh_stderr = self._ssh_client.exec_command(cmd)
            # need some delay to be sure that lib gets copied
            # and then do ftp
            time.sleep(5)
            src_file = '%s/%s'%(self._tmp_dir, lib_name)
            dest_file = '%s/libraries/%s'%(self._parent_dir, lib_name)
            self.do_ftp(src_file, dest_file)
            print('\nCopying library %s : Success' %lib_name)
    # end copy_libraries

    def copy_introspect(self):
        cmd = 'docker exec %s %s read' %(self._container, self._cli)
        # executing above command will return list of all
        # the sub-commands which we will run in loop to
        # collect all the agent introspect logs
        cmd_list = self.get_ssh_cmd_output(cmd).split('\n')
        # delete first and last element as they are not commands
        del cmd_list[0]
        if len(cmd_list) > 0:
            del cmd_list[-1]
        for i in range(len(cmd_list)):
            # remove leading and trailing white spaces if any
            cmd_list[i] = cmd_list[i].strip()
            # create file name for for each command
            tmp_str = cmd_list[i].split()
            file_name = '_'.join(tmp_str)
            file_path = self._parent_dir + '/introspect/' + file_name
            try:
                f = open(file_path, 'a')
            except Exception as e:
                print('\nError opening file %s: %s' %(file_path, e))
                print('\nCopying introspect logs : Failed')
                return
            cmd = 'docker exec %s %s %s' %(self._container,
                        self._cli, cmd_list[i])
            cmd_op = self.get_ssh_cmd_output(cmd)
            f.write(cmd_op)
            f.close()
        print('\nCopying introspect logs : Success')
    # end copy_introspect

    def copy_sandesh_traces(self, path):
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

    def get_controller_ip_list(self):
        # below command will give ip addresses of all the controllers
        cmd = 'docker exec %s %s read xmpp connection status' \
                %(self._container, self._cli)
        cmd_op = self.get_ssh_cmd_output(cmd)
        json_data = json.loads(cmd_op)
        controller_ip_list = []
        for item in json_data['AgentXmppConnectionStatus']['peer']['list']:
            controller_ip_list.append(
                json_data['AgentXmppConnectionStatus']\
                        ['peer']['list'][item]['controller_ip'])
        return controller_ip_list
    # end get_controller_ip_list

    def copy_controller_logs(self):
        for ip in self.get_controller_ip_list():
            # iterate through all the controller in the list
            # and collect logs
            cntr_dir_path = '%s/controller/controller-%s'%(self._parent_dir, ip)
            cmd = 'mkdir %s' %cntr_dir_path
            subprocess.call(cmd, shell=True)
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
                dest_file = '%s/%s'%(cntr_dir_path, filename)
                sftp_client.get(src_file, dest_file)
            sftp_client.close()
        print('\nCopying controller logs: Success')
    # end copy_controller_logs

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
        sftp_client.get(src_file, dest_file)
        sftp_client.close()
    # end do_ftp

    def get_vrouter_logs(self):
        print "\nDumping the output of commands to files"
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
                self.run_command_interval_times(str,30,1) #command,interval,times
            else:
                str_file_name =  str.replace(' ','')
                myCmd = 'docker exec vrouter_vrouter-agent_1 /bin/sh -c "%s" \
                        >> /tmp/vrouter_logs/%s.txt'%(str,str_file_name)
                cmd_op = self.get_ssh_cmd_output(myCmd)
                source_path = '/tmp/vrouter_logs/%s.txt'%(str_file_name)
                dest_path = '%s/vrouter_logs/%s.txt'%(self._parent_dir,str_file_name)
                self.do_ftp(source_path,dest_path)

        self.get_per_vrf_logs()
        self.get_virsh_individual_stats()
    # end get_vrouter_logs


    def get_per_vrf_logs(self):
        print "\nParsing through the vrfstats dump and getting logs per vrf"

        myCmd = 'docker exec vrouter_vrouter-agent_1 /bin/sh -c \
            "vrfstats --dump" >> /tmp/VRF_File1.txt'
        cmd_op = self.get_ssh_cmd_output(myCmd)

        source_path = '/tmp/VRF_File1.txt'
        dest_path = '/tmp/VRF_File2.txt'
        self.do_ftp(source_path,dest_path)

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
                    self.do_ftp(source_path,dest_path)

        myCmd = 'rm -rf /tmp/VRF_File1.txt'
        cmd_op = self.get_ssh_cmd_output(myCmd)

        myCmd = 'rm -rf /tmp/VRF_File2.txt'
        cmd_op = self.get_ssh_cmd_output(myCmd)
        subprocess.call(myCmd,shell=True)

    #end get_per_vrf_logs

    
    def get_virsh_individual_stats(self):
        print "\nParsing through the virsh list and getting logs per virsh"

        myCmd = 'docker exec vrouter_vrouter-agent_1 /bin/sh -c \
            "virsh list" >> /tmp/VIRSH_File1.txt'
        cmd_op = self.get_ssh_cmd_output(myCmd)

        source_path = '/tmp/VIRSH_File1.txt'
        dest_path = '/tmp/VIRSH_File2.txt'
        self.do_ftp(source_path,dest_path)

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
                    self.do_ftp(source_path,dest_path)
        

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
            self.do_ftp(source_path,dest_path)
   #end run_command_interval_times       
    
    def compress_folder(self):
        print("\nCompressing folder %s" %self._parent_dir)
        self._compress_file =  '/var/log/%s.tar.gz' %self._dir_name
        cmd = 'tar -zcf %s %s > /dev/null 2>&1' \
                %(self._compress_file, self._parent_dir)
        subprocess.call(cmd, shell=True)
    #end compress_folder

    def cleanup(self):
        # delete tmp directory
        cmd = 'rm -rf %s' %self._tmp_dir
        ssh_stdin, ssh_stdout, ssh_stderr = self._ssh_client.exec_command(cmd)
        # delete this directory as we have zipped it now
        cmd = 'rm -rf %s' %self._parent_dir
        subprocess.call(cmd, shell=True)
    # end cleanup

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

def help_str():
    print('\npython vrouter_agent_debug_info.py --host <ip> '+ \
            '--port <port> --user <user> --pw <password> -file <path> -c')
    print('\n--host  : agent node ip [mandatory]')
    print('--port  : agent introspect port [optional]')
    print('--user  : username required to ssh to node running agent [optional]')
    print('--pw    : password required to ssh to node runnig agent  [optional]')
    print('--file  : path of instance.yaml file. ' + \
            'Required if user and pw is not provided [optional]')
    print('-c      : generate and copy gcore if provided [optional]\n')


def main():
    argv = sys.argv[1:]

    try:
        host = argv[argv.index('--host') + 1]
    except ValueError:
        help_str()
        sys.exit()

    try:
        port = argv[argv.index('--port') + 1]
    except ValueError:
        port = 8085

    try:
        conf_file = argv[argv.index('--file') + 1]
    except ValueError:
        conf_file = '/root/contrail-ansible-deployer/config/instances.yaml'

    gcore = False
    try:
        if argv[argv.index('-c')]:
            gcore = True
    except ValueError:
        pass

    try:
        user = argv[argv.index('--user') + 1]
    except ValueError:
        user = None

    try:
        pw = argv[argv.index('--pw') + 1]
    except ValueError:
        pw = None

    obj = Debug(dir_name='vrouter-agent-debug-info',
                process_name='contrail-vrouter-agent',
                container_name='vrouter_vrouter-agent_1',
                log_path='/var/log/contrail/',
                lib_path='/usr/lib64/',
                cli='contrail-vrouter-agent-cli',
                config_file=conf_file,
                host=host,
                port=port,
                user=user,
                pw=pw)
    obj.create_directories()
    obj.copy_logs()
    obj.copy_docker_logs()
    obj.copy_contrail_status()
    lib_list = ['libc', 'libstdc++']
    obj.copy_libraries(lib_list)
    if gcore:
        obj.generate_gcore()
        obj.copy_gcore()
    obj.copy_introspect()
    obj.copy_controller_logs()
    obj.copy_sandesh_traces('Snh_SandeshTraceBufferListRequest')
    obj.get_vrouter_logs()
    obj.compress_folder()
    obj.cleanup()
    print('\nComplete logs copied at %s' %obj._compress_file)

if __name__ == '__main__':
    main()

