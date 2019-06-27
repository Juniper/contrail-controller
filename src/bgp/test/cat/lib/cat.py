import sys
import os
import subprocess
import json
import signal
import datetime
import shutil
import time

objects = []

###############################################################
#							      #
#	Main Control-node Agent Test framework Class	      #
#							      #
###############################################################


class CAT:

	direc = ""

	@staticmethod
	def get_report_dir():
		user = os.environ.get('USER')
		udir = '/cs-shared/CAT/reports/' + user
		if not os.path.exists(udir):
			os.makedirs(udir)
		return udir

	@staticmethod
	def clean_up():
		print("Called clean-up")
		for item in objects:
			os.kill(item.pid, signal.SIGKILL)
			# delete file from /tmp/CAT/<user-id>/pid.txt

	@staticmethod
	def sig_cleanup(sig, frame):
		print("Called signal handler here")
		CAT.clean_up()

	@staticmethod
	def initialize():
		print("Initializing ")
		main = "/tmp/CAT/"
		user = main + os.environ.get('USER')
		date = user + "/" + str(datetime.datetime.now())

		if not os.path.exists(main):
			os.makedirs(main)

		if not os.path.exists(user):
			os.makedirs(user)

		if not os.path.exists(date):
			os.makedirs(date)

		CAT.direc = date # /tmp/CAT/UNAME/DATE
	
		signal.signal(signal.SIGINT, CAT.sig_cleanup)
		signal.signal(signal.SIGTERM, CAT.sig_cleanup)

	@staticmethod
	def add_control_node(test, name, port):
		con = ControlNode(test, name, CAT.direc)
		con.create_object(port)
		objects.append(con)
		return con

	@staticmethod
	def add_agent(test, name, objs):
		agent = Agent(test, name, CAT.direc)
		agent.create_agent(objs)
		objects.append(agent)
		return agent

	@staticmethod
	def delete_all_files():
		print("Removing all directories and files")
		direc = CAT.direc
		shutil.rmtree(direc, ignore_errors=False)

	@staticmethod
	def check_connection_internal(cons, agents):
		for item in cons:
			for age in agents:
				url = "curl -s \'http://127.0.0.1:" + str(item.http_port) + "/Snh_BgpNeighborReq?x=" + age.name + "\' | xmllint --format  - | grep -i state | grep -i Established"
				try:
					r = subprocess.check_output(url, stderr=subprocess.STDOUT, shell=True)
				except subprocess.CalledProcessError as e:
					#print("command \'{}\' return with error (code {}): {}".format(e.cmd, e.returncode, e.output))
					return False, str(item.name) + " not connected to " + str(age.name)
		return True, None

	@staticmethod
	def check_connection(cons, agents, retry_time, retry_tries):
		while retry_tries >= 0:
			ret, e = CAT.check_connection_internal(cons, agents)
			if ret == False:
				print("Retrying. retry-time " + str(retry_time) + "seconds")
				retry_tries -= 1
				time.sleep(retry_time)
			else:
				return ret, e
		#print("command \'{}\' return with error (code {}): {}".format(e.cmd, e.returncode, e.output))
		return ret, e


#########################################################
#							#
#	Class for Control-Node				#
#							#
#########################################################


class ControlNode:
	def __init__(self, test, name, home):
		self.name = name
		self.test = test
		self.pid = 0
		self.http_port = 0
		self.bgp_port = 0
		self.xmpp_port = 0
		self.filename = ""
		self.user = home
		self.user_dir = ""
		self.control = ""
		self.conf_file_dir = ""
		self.log_files = ""
		self.create_dir()

	def create_dir(self):
		directory = self.user
		if not os.path.exists(directory):
			os.makedirs(directory)

		test = directory + "/" + self.test
		if not os.path.exists(test):
			os.makedirs(test)

		self.control = test + "/control_node"
		if not os.path.exists(self.control):
			os.makedirs(self.control)

		self.user_dir = self.control + "/" + str(self.name)
		if not os.path.exists(self.user_dir):
			os.makedirs(self.user_dir)

		self.conf_file_dir = self.user_dir + "/conf"
		if not os.path.exists(self.conf_file_dir):
			os.makedirs(self.conf_file_dir)

		self.log_files = self.user_dir + "/log"
		if not os.path.exists(self.log_files):
			os.makedirs(self.log_files)

	def create_object(self, port):
		self.http_port = port
		loop = True
		while loop:
			new_pid = os.fork()
			if new_pid == 0:
				self.pid = os.getpid()
				self.execute_child()
			else:
				self.pid = new_pid
				self.read_port_numbers()
				break
			loop = False

	def read_port_numbers(self):
		self.filename = self.user_dir + "/" + str(self.http_port) + ".txt"
		read = True
		while(read):
			if os.path.exists(self.filename):
				with open(self.filename) as f:
					data = json.load(f)
					for p in data['ControllerDetails']:
						self.xmpp_port = p['XMPPPORT']
						self.bgp_port = p['BGPPORT']	
				read = False
			continue

	def execute_child(self):

		c1 = "/cs-shared/CAT/binaries/bgp_ifmap_xmpp_integration_test"

		env = { "USER": self.user, "BGP_IFMAP_XMPP_INTEGRATION_TEST_SELF_NAME": "overcloud-contrailcontroller-1", "BGP_IFMAP_XMPP_INTEGRATION_TEST_INTROSPECT": str(self.http_port), "BGP_IFMAP_XMPP_INTEGRATION_TEST_PAUSE": "1", "LOG_DISABLE": "1", "USER_DIR": str(self.user_dir)}
		#"BGP_IFMAP_XMPP_INTEGRATION_TEST_DATA_FILE": "/cs-shared/db_dumps/att_contrail_db.json",

		args = []
		os.execve(c1, args, env)


#########################################################
#							#
#	Class for Agent					#
#							#
#########################################################

class Agent:
	def __init__(self, test, name, home):
		self.test = test
		self.pid = 0
		self.xmpp_port = 0
		self.conf_file = ""
		self.name = name
		self.user = home
		self.user_dir = ""
		self.agent = ""
		self.conf_file_dir = ""
		self.log_files = ""
		self.create_agent_dir()

	def create_agent_dir(self):
		directory = self.user
		if not os.path.exists(directory):
			os.makedirs(directory)

		test = directory + "/" + self.test
		if not os.path.exists(test):
			os.makedirs(test)

		self.agent = test + "/agent"
		if not os.path.exists(self.agent):
			os.makedirs(self.agent)

		self.user_dir = self.agent + "/" + str(self.name)
		if not os.path.exists(self.user_dir):
			os.makedirs(self.user_dir)

		self.conf_file_dir = self.user_dir + "/conf"
		if not os.path.exists(self.conf_file_dir):
			os.makedirs(self.conf_file_dir)

		self.log_files = self.user_dir + "/log"
		if not os.path.exists(self.log_files):
			os.makedirs(self.log_files)

	def create_agent(self, objs):
		ports = []
		for i in objs:
			ports.append(i.xmpp_port)
		self.xmpp_ports = ports
		self.create_conf()
		loop = True
		while(loop):
			new_pid = os.fork()
			if new_pid == 0:
				self.pid = os.getpid()
				self.exec_child()
			else:
				self.pid = new_pid
				break;
			loop = False
		return

	def exec_child(self):
		c1 = "/cs-shared/CAT/binaries/contrail-vrouter-agent"
		arg = [c1, "--config_file=" + self.conf_file]

		os.execv(c1, arg)
		

	def create_conf(self):
		sample_conf = "/cs-shared/CAT/configs/contrail-vrouter-agent.conf"
		conf = open(sample_conf, 'r+')
		new_conf = []

		for line in conf:
			if 'xmpp_port' in line:
				line_c = line.split('=')
				line_c[1] = ""
				for p in self.xmpp_ports:
					line_c[1] = line_c[1] + "127.0.0.1:" + str(p) + " "
				updated_line = "=".join(line_c)
				new_conf.append(updated_line)
			elif 'agent_name' in line:
				line_s = line.split('=')
				line_s[1] = str(self.name)
				updated_line = "=".join(line_s)
				new_conf.append(updated_line)
			elif 'log_file' in line:
				line_s = line.split('=')
				line_s[1] = str(self.log_files) + "/" + "contrail-vrouter-agent.log"
				updated_line = "=".join(line_s)
				new_conf.append(updated_line)
			else:
				new_conf.append(line)

		new_conf_file = self.conf_file_dir + "/contrail-vrouter-agent_" + self.name + ".conf"
		new_conf_f = open(new_conf_file, 'w')
		new_conf_f.writelines(new_conf)
		conf.close()
		new_conf_f.close()
		self.conf_file = new_conf_file
