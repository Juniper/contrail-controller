package agent

import (
    "bufio"
    "cat/config"
    "cat/sut"
    "fmt"
    "io/ioutil"
    "log"
    "os"
    "os/exec"
    "path/filepath"
    "strings"
)

const agentName = "control-vrouter-agent"
const agentConfFile = "../agent/contrail-vrouter-agent.conf"
const agentBinary = "../../../../build/debug/vnsw/agent/contrail/contrail-vrouter-agent"
const agentAddPortBinary = "../../../../controller/src/vnsw/agent/port_ipc/vrouter-port-control" 

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
                BGPPort: 0,
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
    if err := a.Start(); err != nil {
        return nil, err
    }
    return a, nil
}

func (a *Agent) Start() error {
    if _, err := os.Stat(agentBinary); os.IsNotExist(err) {
        log.Fatal(err)
    }
    a.Cmd = exec.Command(agentBinary, "--config_file=" + a.Component.ConfFile)
    env := sut.EnvMap{
        "LD_LIBRARY_PATH": "../../../../build/lib",
        "LOGNAME": os.Getenv("USER"),
    }
    a.Cmd.Env = a.Env(env)
    if err := a.Cmd.Start(); err != nil {
        return fmt.Errorf("Failed to start agent: %v", err)
    }
    return nil
}

func (a *Agent) AddVirtualPort(vmi, vm, vn, project *config.ContrailConfigObject, ipv4_address, mac_address, tap_if  string) error {
    cmd := fmt.Sprintf("%s --oper=add --uuid=%s --instance_uuid=%s --vn_uuid=%s --vm_project_uuid=%s --ip_address=%s  --ipv6_address= --vm_name=%s --tap_name=%s --mac=%s --rx_vlan_id=0 --tx_vlan_id=0", agentAddPortBinary, vmi.Uuid, vm.Uuid, vn.Uuid, project.Uuid, ipv4_address, vm.Uuid, tap_if, mac_address)
    _, err := exec.Command("/bin/bash", "-c", cmd).Output()
    return err
}

func (a *Agent) CreateConfiguration() error {
    file, err := os.Open(agentConfFile)
    if err != nil {
        log.Fatal(err)
    }
    defer file.Close()
    var config []string
    scanner := bufio.NewScanner(file)
    for scanner.Scan() {
        line := scanner.Text()
        var confLine string
        switch {
        case strings.Contains(line, "xmpp_port"):
            confLine = "servers="
            for _, p := range a.XMPPPorts {
                confLine = fmt.Sprintf("%s127.0.0.1:%d ", confLine, p)
            }
        case strings.Contains(line, "agent_name"):
            confLine = fmt.Sprintf("agent_name=%s", a.Name)
        case strings.Contains(line, "log_file"):
            confLine = fmt.Sprintf("log_file=%s/%s.log", a.LogDir, a.Name)
        default:
            confLine = line
        }
        config = append(config, confLine)
    }

    a.Component.ConfFile = fmt.Sprintf("%s/%s.conf", a.Component.ConfDir,
                                       a.Component.Name)
    if err := ioutil.WriteFile(a.Component.ConfFile, []byte(strings.Join(config, "\n")), 0644); err != nil {
        return err
    }
    return nil
}
