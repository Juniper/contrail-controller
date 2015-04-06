import socket

class process_stat:
    def __init__(self, pname):
        self.start_count = 0
        self.stop_count = 0
        self.exit_count = 0
        self.start_time = ''
        self.exit_time = ''
        self.stop_time = ''
        self.core_file_list = []
        self.last_exit_unexpected = False
        self.process_state = 'PROCESS_STATE_STOPPED'
        self.group = 'default'
        self.name = socket.gethostname()
