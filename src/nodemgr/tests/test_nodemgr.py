import logging
import mock
import unittest
import nodemgr
import nodemgr.common.event_manager
import nodemgr.control_nodemgr.event_manager
import nodemgr.common.utils
from nodemgr.common.container_process_manager import ContainerProcessInfoManager


logging.basicConfig(level=logging.INFO,
                    format='%(asctime)s %(levelname)s %(message)s')


class Config(object):
    def __init__(self):
        self.collectors = ['0.0.0.0']
        self.sandesh_keyfile = ''
        self.sandesh_certfile = ''
        self.sandesh_ca_cert = ''
        self.sandesh_ssl_enable = False
        self.introspect_ssl_enable = None
        self.introspect_ssl_insecure = None
        self.sandesh_dscp_value = None
        self.disable_object_logs = None
        self.sandesh_send_rate_limit = None
        self.log_local = False
        self.log_category = '*'
        self.log_level = 4
        self.log_file = None
        self.use_syslog = False
        self.syslog_facility = None
        self.hostip = '0.0.0.0'
        self.http_server_ip = '0.0.0.0'
        self.corefile_path = '/var/crashes/'
        self.tcp_keepalive_enable = None
        self.tcp_keepalive_idle_time = None
        self.tcp_keepalive_interval = None
        self.tcp_keepalive_probes = None
        self.hostname = None


class NodemgrTest(unittest.TestCase):

    @mock.patch('os.path.getmtime')
    @mock.patch('glob.glob')
    @mock.patch('os.remove')
    @mock.patch('builtins.open')
    @mock.patch('nodemgr.control_nodemgr.event_manager.ControlEventManager.send_process_state_db')
    @mock.patch('nodemgr.common.utils.is_running_in_docker')
    @mock.patch('nodemgr.common.container_process_manager.ContainerProcessInfoManager')
    @mock.patch('nodemgr.common.container_process_manager.ContainerProcessInfoManager.get_all_processes')
    @mock.patch('nodemgr.common.linux_sys_data.LinuxSysData.get_corefiles')
    @mock.patch('docker.from_env')
    def test_nodemgr(
            self, mock_docker_from_emv, mock_get_core_files, mock_get_all_process, mock_docker_process_info_mgr,
            mock_is_running_in_docker, mock_send_process_state_db, mock_open, mock_remove, mock_glob, mock_tm_time):
        headers = {}
        headers['expected'] = '0'
        headers['pid'] = '123'
        config_obj = Config()
        mock_is_running_in_docker.return_value = True
        mock_docker_process_info_mgr.return_value = ContainerProcessInfoManager('', '', '', '', None)
        list_of_process = []
        process_info = {}
        process_info['pid'] = '123'
        process_info['statename'] = 'expected'
        process_info['start'] = '12:00:00'
        process_info['name'] = 'proc1'
        process_info['group'] = 'default'
        list_of_process.append(process_info)
        mock_get_all_process.return_value = list_of_process
        cm = nodemgr.control_nodemgr.event_manager.ControlEventManager(config_obj, '')
        proc_stat = nodemgr.common.process_stat.ProcessStat('proc1', '0.0.0.0')
        # create 4 core files
        cm.process_state_db['default']['proc1'] = proc_stat
        # IF core file list < 5 entries , no core files should be deleted
        mock_get_core_files.return_value = ['core.proc1.1', 'core.proc1.2', 'core.proc1.3', 'core.proc1.4']
        _ = cm._update_process_core_file_list()
        exp_core_list = ['core.proc1.1', 'core.proc1.2', 'core.proc1.3', 'core.proc1.4']
        # there should be no core files
        self.assertEqual(len(cm.process_state_db['default']['proc1'].core_file_list), 4)
        self.assertEqual(cm.process_state_db['default']['proc1'].core_file_list, exp_core_list)
        # Calls with more core files should change the list of core files
        mock_get_core_files.return_value = ['core.proc1.1', 'core.proc1.2', 'core.proc1.3', 'core.proc1.4', 'core.proc1.5']
        exp_core_list = ['core.proc1.2', 'core.proc1.3', 'core.proc1.4', 'core.proc1.5']
        _ = cm._update_process_core_file_list()
        self.assertEqual(len(cm.process_state_db['default']['proc1'].core_file_list), 4)
