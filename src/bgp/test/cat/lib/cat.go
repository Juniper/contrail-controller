package main

import (
    "bufio"
    "encoding/json"
    "errors"
    "fmt"
    "io/ioutil"
    "log"
    "os"
    "os/exec"
    "os/signal"
    "strings"
    "strconv"
    "syscall"
    "time"
)

func (self *CAT) CreateDir(dir string) {
    if _, err := os.Stat(dir); os.IsNotExist(err) {
        os.MkdirAll(dir, 0700)
    }
}

func (self *CAT) GetReportDir() string {
    self.CreateDir(self.ReportDir)
    return self.ReportDir
}

func (self *CAT) CleanUp() {
    if self.Pause {
        self.__Pause()
    }
    for _, item := range self.ControlNodes {
        syscall.Kill(item.Pid, syscall.SIGKILL)
    }
    for _, item := range self.Agents {
        syscall.Kill(item.Pid, syscall.SIGKILL)
        os.Remove(strconv.Itoa(item.Pid) + ".json")
    }
    os.Remove(self.LogDir)
}

func (self *CAT) __Pause() {
    cmd := exec.Command("/usr/bin/python")
    cmd.Stdin = os.Stdin
    cmd.Stdout = os.Stdout
    cmd.Stderr = os.Stderr
    err := cmd.Run()
    if err != nil {
        log.Fatal(err)
    }
    time.Sleep(2600 * time.Second)
}

func (self *CAT) SigCleanup(sig int, frame int) {
    self.CleanUp()
}

func (self *CAT) __GetHostIp() (ip string, err error) {
    cmd := exec.Command("hostname", "-i")
    out, err := cmd.CombinedOutput()
    if err != nil {
        log.Fatal("Cannot find host ip address")
        return "", err
    }

    ips := strings.Split(string(out), " ")
    for _, ip := range ips {
        if !strings.HasPrefix(ip, "127.") {
            return strings.Trim(ip, "\n"), nil
        }
    }
    return "", errors.New("Cannot retrieve host ip address")
}

func (self *CAT) Initialize() *CAT {
    cmd := exec.Command("/bin/date")
    ds, _ := cmd.CombinedOutput()
    self.LogDir = "/tmp/CAT/" + os.Getenv("USER") + self.LogDir + "/" +
                  strings.Replace(strings.TrimSpace(string(ds)), " ", "_", -1)
    self.CreateDir(self.LogDir)
    fmt.Println("Logs are in " + self.LogDir)
    self.Ip, _ = self.__GetHostIp()
    signalCh := make(chan os.Signal, 1)
    signal.Notify(signalCh, os.Interrupt)
    // signal.signal(signal.SIGTERM, CAT.sig_cleanup)
    return self
}

func (self *CAT) AddControlNode (test string, name string,
                                 port int) *ControlNode {
    con := new(ControlNode).Initialize(test, name, self)
    con.Verbose = self.Verbose
    con.HttpPort = port
    con.Pid = con.Start()
    con.ReadPortNumbers()
    self.ControlNodes = append(self.ControlNodes, con)
    return con
}

func (self *ControlNode) ReadPortNumbers() {
    self.PortsFile = self.ConfDir + "/" + strconv.Itoa(self.Pid) + ".json"
    read := 30
    var err error
    for (read > 0) {
        jsonFile, err := os.Open(self.PortsFile)
        defer jsonFile.Close()
        if err != nil {
            time.Sleep(1 * time.Second)
            read = read - 1
            continue
        }
        bytes, _ := ioutil.ReadAll(jsonFile)
        json.Unmarshal(bytes, &self.Ports)
        return
    }
    log.Fatal(err)
}

func (self *ControlNode) _CheckXmppConnection(agent *Agent) bool {
    url := "/usr/bin/curl -s http://127.0.0.1:" +
        strconv.Itoa(self.Ports.HttpPort) + "/Snh_BgpNeighborReq?x=" +
        agent.Name +
        " | xmllint --format  - | grep -i state | grep -i Established"
    _, err := exec.Command("/bin/bash", "-c", url).Output()
    return err == nil
}

func (self *ControlNode) _CheckXmppConnections(agents []*Agent) bool {
    for i := 0; i < len(agents); i++ {
        if self._CheckXmppConnection(agents[i]) == false {
            return false
        }
    }
    return true
}

func (self *ControlNode) CheckXmppConnections(agents []*Agent,
        retry int, wait time.Duration) bool {
    for r := 0; r < retry; r++ {
        if self._CheckXmppConnections(agents) {
            return true
        }
        time.Sleep(wait * time.Second)
    }
    return false
}

func (self *CAT) AddAgent(test string, name string,
                          control_nodes []*ControlNode) *Agent {
    agent := new(Agent).Initialize(test, name, self)
    agent.CreateAgent(control_nodes)
    self.Agents = append(self.Agents, agent)
    return agent
}

func (self *CAT) DeleteAllFile() {
    return
}

func (self *CAT) RedirectStdOutErr() {
    /*
    old := os.dup(1)
    os.close(1)
    f = self.log_files + "/" + self.Name + ".log"
    open(f, 'w')
    os.open(f, os.O_WRONLY)

    old = os.dup(2)
    os.close(2)
    f = self.log_files + "/" + self.Name + ".log"
    open(f, 'w')
    os.open(f, os.O_WRONLY)
    */
}

func (self *Component) SendSignal(signal syscall.Signal) {
    syscall.Kill(self.Pid, signal) // syscall.SIGHUP
}

func (self *ControlNode) Initialize(test, name string, cat *CAT) *ControlNode {
    self.Cat = cat
    self.Name = name
    self.Pid = 0
    self.HttpPort = 0
    self.XmppPort = 0
    self.PortsFile = ""

    self.Verbose = false
    self.ConfDir = cat.LogDir + "/" + test + "/control-node/" + name + "/" +
                   "conf"
    self.LogDir = cat.LogDir + "/" + test + "/control-node/" + name + "/" +
                   "log"
    self.Cat.CreateDir(self.LogDir)
    self.Cat.CreateDir(self.ConfDir)
    return self
}

func (self *ControlNode) Start() int {
    c1 :=
      "../../../../../../build//debug/bgp/test/bgp_ifmap_xmpp_integration_test"
    if _, err := os.Stat(c1); os.IsNotExist(err) {
        log.Fatal(err)
    }

    env := map[string] string {
"USER": os.Getenv("USER"),
"BGP_IFMAP_XMPP_INTEGRATION_TEST_SELF_NAME": "overcloud-contrailcontroller-1",
"CAT_BGP_PORT": strconv.Itoa(self.Ports.BgpPort),
"CAT_XMPP_PORT": strconv.Itoa(self.Ports.XmppPort),
"BGP_IFMAP_XMPP_INTEGRATION_TEST_INTROSPECT": strconv.Itoa(self.Ports.HttpPort),
"BGP_IFMAP_XMPP_INTEGRATION_TEST_PAUSE": "1",
"LOG_DISABLE" : strconv.FormatBool(self.Verbose),
"BGP_IFMAP_XMPP_INTEGRATION_TEST_DATA_FILE":
"/cs-shared/CAT/configs/bulk_sync_2.json",
"LD_LIBRARY_PATH": "../../../../../../build/lib",
"CONTRAIL_CAT_FRAMEWORK": "1",
"USER_DIR": self.ConfDir + "/..",
    }
    var enva []string
    for k, v := range env {
        enva = append(enva, k + "=" + v)
    }
    cmd := exec.Command(c1)
    cmd.Env = enva
    err := cmd.Start()
    if err != nil {
        log.Fatal(err)
    }
    return cmd.Process.Pid
}

func (self *ControlNode) Restart() {
    syscall.Kill(self.Pid, syscall.SIGKILL)
    self.Pid = self.Start()
}

func (self *Agent) Initialize(test, name string, cat *CAT) *Agent {
    self.Cat = cat
    self.Pid = 0
    self.XmppPort = 0
    self.ConfFile = ""
    self.Name = name
    self.ConfDir = cat.LogDir + "/" + test + "/control-vrouter-agent/" +
                   name + "/" + "conf"
    self.LogDir = cat.LogDir + "/" + test + "/control-vrouter-agent/" +
                   name + "/" + "log"
    self.Cat.CreateDir(self.ConfDir)
    self.Cat.CreateDir(self.LogDir)
    return self
}

func (self *Agent) CreateAgent(control_nodes []*ControlNode) {
    var ports []int
    for _, i := range control_nodes {
        ports = append(ports, i.Ports.XmppPort)
    }
    self.XmppPorts = ports
    self.CreateConfiguration()
    self.Pid = self.Start()
}

func (self *Agent) Start() int {
    c1 :=
     "../../../../../../build/debug/vnsw/agent/contrail/contrail-vrouter-agent"
    if _, err := os.Stat(c1); os.IsNotExist(err) {
        log.Fatal(err)
    }
    env := map[string] string {
        "LD_LIBRARY_PATH": "../../../../../../build/lib",
        "LOGNAME": os.Getenv("USER"),
    }
    var enva []string
    for k, v := range env {
        enva = append(enva, k + "=" + v)
    }
    cmd := exec.Command(c1, "--config_file=" + self.ConfFile)
    cmd.Env = enva
    err := cmd.Start()
    if err != nil {
        log.Fatal(err)
    }
    return cmd.Process.Pid
}

func (self *Agent) CreateConfiguration() {
    sample_conf := "contrail-vrouter-agent.conf"
    file, err := os.Open(sample_conf)
    if err != nil {
        log.Fatal(err)
    }
    defer file.Close()
    var new_conf []string
    scanner := bufio.NewScanner(file)
    for scanner.Scan() {
        line := scanner.Text()
        if strings.Contains(line, "xmpp_port") {
            updated_line := "servers="
            for _, p := range(self.XmppPorts) {
                updated_line=updated_line + "127.0.0.1:" + strconv.Itoa(p) + " "
            }
            new_conf = append(new_conf, updated_line)
        } else if strings.Contains(line, "agent_name") {
            new_conf = append(new_conf, "agent_name=" + self.Name)
        } else if strings.Contains(line, "log_file") {
            new_conf = append(new_conf, "log_file=" + self.LogDir + "/" +
                                        self.Name + ".log")
        } else {
            new_conf = append(new_conf, line)
        }
    }

    self.ConfFile = self.ConfDir + "/" + self.Name + ".conf"
    file, err = os.Create(self.ConfFile)
    if err != nil {
        log.Fatal(err)
    }
    defer file.Close()

    for _, line := range new_conf {
        file.WriteString(line + "\n");
    }
}
