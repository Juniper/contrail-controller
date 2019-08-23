package agent

import (
    "bufio"
    "cat/sut"
    "fmt"
    "log"
    "os"
    "os/exec"
    "path/filepath"
    "strings"
    "strconv"
)

const agentName = "control-vrouter-agent"

// Agent is a SUT component for VRouter Agent.
type Agent struct {
    sut.Component
    XMPPPorts []int
}

func New(m sut.Manager, name, test string, xmpp_ports []int) (*Agent, error) {
    a := &Agent{
        Component: sut.Component{
            Name:    name,
            Manager: m,
            LogDir:  filepath.Join(m.RootDir, test, agentName, name, "log"),
            ConfDir: filepath.Join(m.RootDir, test, agentName, name, "conf"),
            Config: sut.Config{
                Pid:      0,
                HTTPPort: 0,
                XMPPPort: 0,
            },
            ConfFile: "",
        },
        XMPPPorts: xmpp_ports,
    }
    if err := os.MkdirAll(a.Component.ConfDir, 0755); err != nil {
        return nil, fmt.Errorf("failed to make conf directory: %v", err)
    }
    if err := os.MkdirAll(a.Component.LogDir, 0755); err != nil {
        return nil, fmt.Errorf("failed to make log directory: %v", err)
    }
    a.CreateConfiguration()
    a.Component.Config.Pid = a.Start()
    return a, nil
}

func (a *Agent) Start() int {
    c1:= "../../../../build/debug/vnsw/agent/contrail/contrail-vrouter-agent"
    if _, err := os.Stat(c1); os.IsNotExist(err) {
        log.Fatal(err)
    }
    env := map[string]string{
        "LD_LIBRARY_PATH": "../../../../build/lib",
        "LOGNAME":         os.Getenv("USER"),
    }
    var enva []string
    for k, v := range env {
        enva = append(enva, k+"="+v)
    }
    cmd := exec.Command(c1, "--config_file="+a.Component.ConfFile)
    cmd.Env = enva
    err := cmd.Start()
    if err != nil {
        log.Fatal(err)
    }
    return cmd.Process.Pid
}

func (a *Agent) CreateConfiguration() {
    sample_conf := "../agent/contrail-vrouter-agent.conf"
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
            for _, p := range a.XMPPPorts {
                updated_line = updated_line + "127.0.0.1:" + strconv.Itoa(p) +
                               " "
            }
            new_conf = append(new_conf, updated_line)
        } else if strings.Contains(line, "agent_name") {
            new_conf = append(new_conf, "agent_name="+a.Component.Name)
        } else if strings.Contains(line, "log_file") {
            new_conf = append(new_conf, "log_file="+a.Component.LogDir+"/"+
                a.Component.Name+".log")
        } else {
            new_conf = append(new_conf, line)
        }
    }

    a.Component.ConfFile = a.Component.ConfDir + "/" + a.Component.Name +".conf"
    file, err = os.Create(a.Component.ConfFile)
    if err != nil {
        log.Fatal(err)
    }
    defer file.Close()

    for _, line := range new_conf {
        file.WriteString(line + "\n")
    }
}
