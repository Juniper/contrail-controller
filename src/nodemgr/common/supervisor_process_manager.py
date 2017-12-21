#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

import select
import subprocess
import sys
import xmlrpclib
try:
    from supervisor import childutils, xmlrpc
    supervisor_event_listener_cls_type = childutils.EventListenerProtocol
except:
    supervisor_event_listener_cls_type = object

from pysandesh.util import UTCTimestampUsec

from process_mem_cpu import ProcessMemCpuUsageData


class SupervisorEventListener(supervisor_event_listener_cls_type):
    def __init__(self):
        self.supervisor_events_ctr = 0
        self.supervisor_events_timestamp = None
        self.supervisor_events_error_ctr = 0
        self.supervisor_events_error_timestamp = None

    def wait(self, stdin=sys.stdin, stdout=sys.stdout):
        self.ready(stdout)
        while 1:
            if select.select([stdin], [], [])[0]:
                line = stdin.readline()
                if line is not None:
                    self.supervisor_events_ctr += 1
                    self.supervisor_events_timestamp = str(UTCTimestampUsec())
                    break
                else:
                    self.supervisor_events_error_ctr += 1
                    self.supervisor_events_error_timestamp = str(UTCTimestampUsec())
        headers = childutils.get_headers(line)
        payload = stdin.read(int(headers['len']))
        return headers, payload
        # end wait

# end class SupervisorEventListenerProtocol


class SupervisorProcessInfoManager(object):
    def __init__(self, stdin, stdout, server_url, event_handlers, update_process_list=False):
        self._stdin = stdin
        self._stdout = stdout
        # ServerProxy won't allow us to pass in a non-HTTP url,
        # so we fake the url we pass into it and always use the transport's
        # 'serverurl' to figure out what to attach to
        self._proxy = xmlrpclib.ServerProxy('http://127.0.0.1',
                                            transport=xmlrpc.SupervisorTransport(serverurl=server_url))
        self._event_listener = SupervisorEventListener()
        self._event_handlers = event_handlers
        self._update_process_list = update_process_list

    # end __init__

    def get_all_processes(self):
        """
            Supervisor XMLRPC API returns an array of per process information:
            {'name':           'process name',
             'group':          'group name',
             'description':    'pid 18806, uptime 0:03:12'
             'start':          1200361776,
             'stop':           0,
             'now':            1200361812,
             'state':          1,
             'statename':      'RUNNING',
             'spawnerr':       '',
             'exitstatus':     0,
             'logfile':        '/path/to/stdout-log', # deprecated, b/c only
             'stdout_logfile': '/path/to/stdout-log',
             'stderr_logfile': '/path/to/stderr-log',
             'pid':            1}
        """
        process_infos = self._proxy.supervisor.getAllProcessInfo()
        for process_info in process_infos:
            process_info['statename'] = 'PROCESS_STATE_' + process_info['statename']
            if 'start' in process_info:
                process_info['start'] = process_info['start'] * 1000000
        return process_infos

    def run(self, test):
        while True:
            # we explicitly use self.stdin, self.stdout, and self.stderr
            # instead of sys.* so we can unit test this code
            headers, payload = self._event_listener.wait(
                self._stdin, self._stdout)
            pheaders, pdata = childutils.eventdata(payload + '\n')
            # check for process state change events
            if headers['eventname'].startswith("PROCESS_STATE"):
                """
                    eventname:PROCESS_STATE_STARTING processname:cat groupname:cat from_state:STOPPED tries:0
                    eventname:PROCESS_STATE_RUNNING processname:cat groupname:cat from_state:STARTING pid:2766
                    eventname:PROCESS_STATE_BACKOFF processname:cat groupname:cat from_state:STOPPED tries:0
                    eventname:PROCESS_STATE_STOPPING processname:cat groupname:cat from_state:STARTING pid:2766
                    eventname:PROCESS_STATE_EXITED processname:cat groupname:cat from_state:RUNNING expected:0 pid:2766
                    eventname:PROCESS_STATE_STOPPED processname:cat groupname:cat from_state:STOPPING pid:2766
                    eventname:PROCESS_STATE_FATAL processname:cat groupname:cat from_state:BACKOFF
                    eventname:PROCESS_STATE_UNKNOWN processname:cat groupname:cat from_state:BACKOFF
                """
                process_info = {}
                process_info['name'] = pheaders['processname']
                process_info['group'] = pheaders['groupname']
                process_info['state'] = headers['eventname']
                if 'pid' in pheaders:
                    process_info['pid'] = pheaders['pid']
                if 'expected' in pheaders:
                    process_info['expected'] = int(pheaders['expected'])
                self._event_handlers['PROCESS_STATE'](process_info)
                if self._update_process_list:
                    self._event_handlers['PROCESS_LIST_UPDATE']()
            # check for flag value change events
            if headers['eventname'].startswith("PROCESS_COMMUNICATION"):
                self._event_handlers['PRCOESS_COMMUNICATION'](pdata)
            self._event_listener.ok(self._stdout)
            # end Run

    def get_mem_cpu_usage_data(self, pid, last_cpu, last_time):
        return ProcessMemCpuUsageData(pid, last_cpu, last_time)

    def find_pid(self, name, pattern):
        pid = None
        cmd = "ps -aux | grep " + pattern + " | awk '{print $2}' | head -n1"
        proc = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, close_fds=True)
        stdout, _ = proc.communicate()
        if stdout is not None and stdout != '':
            pid = int(stdout.strip('\n'))
        return pid

# end class SupervisorProcessInfoManager
