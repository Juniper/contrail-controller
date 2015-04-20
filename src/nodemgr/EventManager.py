import gevent
import json
import ConfigParser
from StringIO import StringIO
from ConfigParser import NoOptionError, NoSectionError
import sys
import socket
import time
import subprocess
from subprocess import Popen, PIPE
import supervisor.xmlrpc
import xmlrpclib

from supervisor import childutils
from nodemgr.EventListenerProtocolNodeMgr import EventListenerProtocolNodeMgr
from sandesh_common.vns.constants import INSTANCE_ID_DEFAULT

class EventManager:
    rules_data = []
    group_names = []
    process_state_db = {}
    FAIL_STATUS_DUMMY       = 0x1
    FAIL_STATUS_DISK_SPACE  = 0x2
    FAIL_STATUS_SERVER_PORT = 0x4
    FAIL_STATUS_NTP_SYNC    = 0x8
    def __init__(self, rule_file, discovery_server, discovery_port, collector_addr):
        self.stdin = sys.stdin
        self.stdout = sys.stdout
        self.stderr = sys.stderr
        self.rule_file = rule_file
        self.rules_data = ''
        self.max_cores = 4
        self.max_old_cores = 3
        self.max_new_cores = 1
        self.all_core_file_list = []
        self.core_dir_modified_time = 0
        self.tick_count = 0
        self.fail_status_bits = 0
        self.prev_fail_status_bits = 1
        self.instance_id = INSTANCE_ID_DEFAULT
        self.discovery_server = discovery_server
        self.discovery_port = discovery_port
        self.collector_addr = collector_addr
        self.listener_nodemgr = EventListenerProtocolNodeMgr()
        self.sandesh_global = None

    def add_current_process(self, process_stat):
        proxy = xmlrpclib.ServerProxy('http://127.0.0.1',
                transport=supervisor.xmlrpc.SupervisorTransport(None, None, serverurl=self.supervisor_serverurl))
        # Add all current processes to make sure nothing misses the radar
        for proc_info in proxy.supervisor.getAllProcessInfo():
            if (proc_info['name'] != proc_info['group']):
                proc_name = proc_info['group']+ ":" + proc_info['name']
            else:
                proc_name = proc_info['name']
            process_stat_ent = process_stat(proc_name)
            process_stat_ent.process_state = "PROCESS_STATE_" + proc_info['statename']
            if (process_stat_ent.process_state  ==
                    'PROCESS_STATE_RUNNING'):
                process_stat_ent.start_time = str(proc_info['start']*1000000)
                process_stat_ent.start_count += 1
            self.process_state_db[proc_name] = process_stat_ent
            sys.stderr.write("\nAdding:" + str(proc_info) + "\n")

    def read_config_data(self, config_file):
        data = StringIO('\n'.join(line.strip() for line in open(config_file)))
        Config = ConfigParser.SafeConfigParser()
        Config.readfp(data)
        return Config

    def read_discovery_server_from_conf(self, Config):
        try:
            self.discovery_server = Config.get("DISCOVERY", "server")
        except NoOptionError as e:
            sys.stderr.write("ERROR: " + str(e) + '\n')
        except NoSectionError as e:
            sys.stderr.write("ERROR: " + str(e) + '\n')
        #Hack becos of Configparser and the conf file format itself
        try:
            self.discovery_server[:self.discovery_server.index('#')].strip()
        except:
            self.discovery_server.strip()

    def read_discovery_port_from_conf(self, Config):
        try:
            self.discovery_port = Config.get("DISCOVERY", "port")
        except NoOptionError as e:
            sys.stderr.write("ERROR: " + str(e) + '\n')
        except NoSectionError as e:
            sys.stderr.write("ERROR: " + str(e) + '\n')
        #Hack becos of Configparser and the conf file format itself
        try:
            self.discovery_port = self.discovery_port[:self.discovery_port.index('#')].strip()
        except Exception:
            pass

    def get_discovery_client(self, Config):
        try:
            import discovery.client as client
        except:
            import discoveryclient.client as client
        if self.discovery_server == '':
            self.read_discovery_server_from_conf(Config)
            if self.discovery_server == '':
                self.discovery_server = socket.gethostname()
        if self.discovery_port == -1:
            self.read_discovery_port_from_conf(Config)
            if self.discovery_port == -1:
                self.discovery_port = 5998
        _disc= client.DiscoveryClient(self.discovery_server, self.discovery_port, self.module_id)
        return _disc

    def read_collector_list_from_conf(self, Config):
        try:
            collector_list = Config.get("COLLECTOR", "server_list")
            try:
                collector_list = collector_list[:collector_list.index('#')].strip()
            except:
                collector_list.strip()
            self.collector_addr = collector_list.split()
        except NoOptionError as e:
            sys.stderr.write("ERROR: " + str(e) + '\n')
        except NoSectionError as e:
            sys.stderr.write("ERROR: " + str(e) + '\n')

    def check_ntp_status(self):
        ntp_status_cmd = 'ntpq -n -c pe | grep "^*"'
        proc = Popen(ntp_status_cmd, shell=True, stdout=PIPE, stderr=PIPE)
        (output, errout) = proc.communicate()
        if proc.returncode != 0:
            self.fail_status_bits |= self.FAIL_STATUS_NTP_SYNC
        else:
            self.fail_status_bits &= ~self.FAIL_STATUS_NTP_SYNC
        self.send_nodemgr_process_status()

    def send_process_state_db_base(self, group_names, ProcessInfo, NodeStatus, NodeStatusUVE):
        name = socket.gethostname()
        for group in group_names:
            process_infos = []
            for key in self.process_state_db:
                pstat = self.process_state_db[key]
                if (pstat.group != group):
                    continue
                process_info = ProcessInfo()
                process_info.process_name = key
                process_info.process_state = pstat.process_state
                process_info.start_count = pstat.start_count
                process_info.stop_count = pstat.stop_count
                process_info.exit_count = pstat.exit_count
                process_info.last_start_time = pstat.start_time
                process_info.last_stop_time = pstat.stop_time
                process_info.last_exit_time = pstat.exit_time
                process_info.core_file_list = pstat.core_file_list
                process_infos.append(process_info)
                name = pstat.name

            if not process_infos:
                continue

            # send node UVE
            node_status = NodeStatus()
            node_status.name = name
            node_status.process_info = process_infos
            node_status.all_core_file_list = self.all_core_file_list
            node_status_uve = NodeStatusUVE(data = node_status)
            sys.stderr.write('Sending UVE:' + str(node_status_uve))
            node_status_uve.send()

    def send_all_core_file(self):
        stat_command_option = "stat --printf=%Y /var/crashes"
        modified_time = Popen(stat_command_option.split(), stdout=PIPE).communicate()
        if modified_time[0] == self.core_dir_modified_time:
            return
        self.core_dir_modified_time = modified_time[0]
        ls_command_option = "ls /var/crashes"
        (corename, stderr) = Popen(ls_command_option.split(), stdout=PIPE).communicate()
        self.all_core_file_list = corename.split('\n')[0:-1]
        self.send_process_state_db(self.group_names)

    def send_process_state(self, pname, pstate, pheaders):
        # update process stats
        if pname in self.process_state_db.keys():
            proc_stat = self.process_state_db[pname]
        else:
            proc_stat = process_stat(pname)
            if not proc_stat.group in self.group_names:
                self.group_names.append(proc_stat.group)

        proc_stat.process_state = pstate

        send_uve = False
        if (pstate == 'PROCESS_STATE_RUNNING'):
            proc_stat.start_count += 1
            proc_stat.start_time = str(int(time.time()*1000000))
            send_uve = True

        if (pstate == 'PROCESS_STATE_STOPPED'):
            proc_stat.stop_count += 1
            send_uve = True
            proc_stat.stop_time = str(int(time.time()*1000000))
            proc_stat.last_exit_unexpected = False

        if (pstate == 'PROCESS_STATE_EXITED'):
            proc_stat.exit_count += 1
            send_uve = True
            proc_stat.exit_time = str(int(time.time()*1000000))
            if not(int(pheaders['expected'])):
                self.stderr.write(pname + " with pid:" + pheaders['pid'] + " exited abnormally\n")
                proc_stat.last_exit_unexpected = True
                # check for core file for this exit
                find_command_option = "find /var/crashes -name core.[A-Za-z]*."+ pheaders['pid'] + "*"
                self.stderr.write("find command option for cores:" + find_command_option + "\n")
                (corename, stderr) = Popen(find_command_option.split(), stdout=PIPE).communicate()
                self.stderr.write("core file: " + corename + "\n")

                if ((corename is not None) and (len(corename.rstrip()) >= 1)):
                    # before adding to the core file list make sure that we do not have too many cores
                    sys.stderr.write('core_file_list:'+str(proc_stat.core_file_list)+", self.max_cores:"+str(self.max_cores)+"\n")
                    if (len(proc_stat.core_file_list) == self.max_cores):
                        # get rid of old cores
                        sys.stderr.write('max # of cores reached:' + str(self.max_cores) + "\n")
                        core_files_to_be_deleted = proc_stat.core_file_list[self.max_old_cores:(self.max_cores - self.max_new_cores+1)]
                        sys.stderr.write('deleting core file list:' + str(core_files_to_be_deleted) + "\n")
                        for core_file in core_files_to_be_deleted:
                            sys.stderr.write('deleting core file:' + core_file + "\n")
                            try:
                                os.remove(core_file)
                            except:
                                pass
                        # now delete the list as well
                        del proc_stat.core_file_list[self.max_old_cores:(self.max_cores - self.max_new_cores+1)]
                    # now add the new core to the core file list
                    proc_stat.core_file_list.append(corename.rstrip())
                    sys.stderr.write("# of cores for " + pname + ":" + str(len(proc_stat.core_file_list)) + "\n")

        # update process state database
        self.process_state_db[pname] = proc_stat
        f = open('/var/log/contrail/process_state' + self.node_type + ".json", 'w')
        f.write(json.dumps(self.process_state_db, default=lambda obj: obj.__dict__))

        if not(send_uve):
            return

        if (send_uve):
            self.send_process_state_db([proc_stat.group])

    def send_nodemgr_process_status_base(self, ProcessStateNames, ProcessState, ProcessStatus, NodeStatus, NodeStatusUVE):
        if (self.prev_fail_status_bits != self.fail_status_bits):
            self.prev_fail_status_bits = self.fail_status_bits
            fail_status_bits = self.fail_status_bits
            state, description = self.get_process_state(fail_status_bits)
            process_status = ProcessStatus(module_id = self.module_id, instance_id = self.instance_id, state = state,
                description = description)
            process_status_list = []
            process_status_list.append(process_status)
            node_status = NodeStatus(name = socket.gethostname(),
                process_status = process_status_list)
            node_status_uve = NodeStatusUVE(data = node_status)
            sys.stderr.write('Sending UVE:' + str(node_status_uve))
            node_status_uve.send()

    def send_disk_usage_info_base(self, NodeStatusUVE, NodeStatus, DiskPartitionUsageStats):
        partition = subprocess.Popen("df -T -t ext2 -t ext3 -t ext4 -t xfs",
              shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        disk_usage_infos = []
        for line in partition.stdout:
            if 'Filesystem' in line:
                continue
            partition_name = line.rsplit()[0]
            partition_type = line.rsplit()[1]
            partition_space_used_1k = line.rsplit()[3]
            partition_space_available_1k = line.rsplit()[4]
            disk_usage_stat = DiskPartitionUsageStats()
            try:
                disk_usage_stat.partition_type = str(partition_type)
                disk_usage_stat.partition_name = str(partition_name)
                disk_usage_stat.partition_space_used_1k = int(partition_space_used_1k)
                disk_usage_stat.partition_space_available_1k = int(partition_space_available_1k)
            except ValueError:
                sys.stderr.write("Failed to get local disk space usage" + "\n")
        disk_usage_infos.append(disk_usage_stat)

        # send node UVE
        node_status = NodeStatus(name = socket.gethostname(),
                disk_usage_info = disk_usage_infos)
        node_status_uve = NodeStatusUVE(data = node_status)
        sys.stderr.write('Sending UVE:' + str(node_status_uve))
        node_status_uve.send()
    # end send_disk_usage_info

    def get_process_state_base(self, fail_status_bits, ProcessStateNames, ProcessState):
        if fail_status_bits:
            state = ProcessStateNames[ProcessState.NON_FUNCTIONAL]
            description = self.get_failbits_nodespecific_desc(fail_status_bits)
            if (description is ""):
                if fail_status_bits & self.FAIL_STATUS_NTP_SYNC:
                    if description != "":
                        description += " "
                    description += "NTP state unsynchronized."
        else:
            state = ProcessStateNames[ProcessState.FUNCTIONAL]
            description = ''
        return state, description

    def get_failbits_nodespecific_desc(self, fail_status_bits):
        return ""

    def event_process_state(self, pheaders, headers):
        self.stderr.write("process:" + pheaders['processname'] + "," + "groupname:" + pheaders['groupname'] + "," + "eventname:" + headers['eventname'] + '\n')
        pname = pheaders['processname']
        if (pheaders['processname'] != pheaders['groupname']):
             pname = pheaders['groupname'] + ":" + pheaders['processname']
        self.send_process_state(pname, headers['eventname'], pheaders)
        for rules in self.rules_data['Rules']:
            if 'processname' in rules:
                 if ((rules['processname'] == pheaders['groupname']) and (rules['process_state'] == headers['eventname'])):
                      self.stderr.write("got a hit with:" + str(rules) + '\n')
                      # do not make async calls
                      try:
                          ret_code = subprocess.call([rules['action']],
                               shell=True, stdout=self.stderr,
                               stderr=self.stderr)
                      except Exception as e:
                          self.stderr.write('Failed to execute action: ' \
                               + rules['action'] + ' with err ' + str(e) + '\n')
                      else:
                          if ret_code:
                              self.stderr.write('Execution of action ' + \
                                   rules['action'] + ' returned err ' + \
                                   str(ret_code) + '\n')

    def event_process_communication(self, pdata):
        flag_and_value = pdata.partition(":")
        self.stderr.write("Flag:" + flag_and_value[0] + " Value:" + flag_and_value[2] + "\n")
        for rules in self.rules_data['Rules']:
            if 'flag_name' in rules:
                if ((rules['flag_name'] == flag_and_value[0]) and (rules['flag_value'].strip() == flag_and_value[2].strip())):
                     self.stderr.write("got a hit with:" + str(rules) + '\n')
                     cmd_and_args = ['/usr/bin/bash', '-c' , rules['action']]
                     subprocess.Popen(cmd_and_args)

    def event_tick_60(self, prev_current_time):
        self.tick_count += 1
        # send other core file
        self.send_all_core_file()
        # send disk usage info periodically
        self.send_disk_usage_info()
        # typical ntp sync time is about 3, 4 min - first time, we scan only after 5 min
        if self.tick_count > 5:
            self.check_ntp_status()

        current_time = int(time.time())
        if ((abs(current_time - prev_current_time)) > 300):
            #update all process start_times with the updated time
            #Compute the elapsed time and subtract them from current time to get updated values
            sys.stderr.write("Time lapse detected " + str(abs(current_time - prev_current_time)) + "\n")
            for key in self.process_state_db:
                pstat = self.process_state_db[key]
                if pstat.start_time is not '':
                    pstat.start_time = str((int(current_time - (prev_current_time-((int)(pstat.start_time))/1000000)))*1000000)
                if (pstat.process_state == 'PROCESS_STATE_STOPPED'):
                    if pstat.stop_time is not '':
                        pstat.stop_time = str(int(current_time - (prev_current_time-((int)(pstat.stop_time))/1000000))*1000000)
                if (pstat.process_state == 'PROCESS_STATE_EXITED'):
                    if pstat.exit_time is not '':
                        pstat.exit_time = str(int(current_time - (prev_current_time-((int)(pstat.exit_time))/1000000))*1000000)
                # update process state database
                self.process_state_db[key] = pstat
            try:
                f = open('/var/log/contrail/process_state' + self.node_type + ".json", 'w')
                f.write(json.dumps(self.process_state_db, default=lambda obj: obj.__dict__))
            except:
                sys.stderr.write("Unable to write json")
                pass
            self.send_process_state_db(self.group_names)
        prev_current_time = int(time.time())
        return prev_current_time

    def runforever(self, test=False):
        prev_current_time = int(time.time())
        while 1:
            gevent.sleep(1)
            # we explicitly use self.stdin, self.stdout, and self.stderr
            # instead of sys.* so we can unit test this code
            headers, payload = self.listener_nodemgr.wait(self.stdin, self.stdout)
            pheaders, pdata = childutils.eventdata(payload+'\n')

            # check for process state change events
            if headers['eventname'].startswith("PROCESS_STATE"):
                self.event_process_state(pheaders, headers)
            # check for flag value change events
            if headers['eventname'].startswith("PROCESS_COMMUNICATION"):
                self.event_process_communication(pdata)
            # do periodic events
            if headers['eventname'].startswith("TICK_60"):
                prev_current_time = self.event_tick_60(prev_current_time)
            self.listener_nodemgr.ok(self.stdout)
