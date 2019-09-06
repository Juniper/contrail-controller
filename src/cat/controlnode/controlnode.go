/*
 * copyright (c) 2019 juniper networks, inc. all rights reserved.
 */

package controlnode

import (
    "cat/agent"
    "cat/sut"
    "encoding/json"
    "fmt"
    "io/ioutil"
    log "github.com/sirupsen/logrus"
    "os"
    "os/exec"
    "path/filepath"
    "strconv"
    "time"
)

// ControlNode represents a contrail-control (control-node) process under CAT framework.
type ControlNode struct {
    sut.Component
    confFile string
}

const controlNodeName = "control-node"
const controlNodeBinary = "../../../../build/debug/bgp/test/bgp_ifmap_xmpp_integration_test"
const controlNodeConfFile = "../../../../controller/src/ifmap/client/testdata/bulk_sync.json"

func New(m sut.Manager, name, ip_address, conf_file, test string, http_port int) (*ControlNode, error) {
    c := &ControlNode{
        Component: sut.Component{
            Name:    name,
            IPAddress: ip_address,
            Manager: m,
            LogDir: filepath.Join(m.RootDir,test,controlNodeName, name, "log"),
            ConfDir: filepath.Join(m.RootDir,test,controlNodeName, name, "conf"),
            Config: sut.Config{
                BGPPort: http_port,
                HTTPPort: http_port,
                XMPPPort: 0,
            },
            ConfFile: conf_file,
        },
    }
    if err := os.MkdirAll(c.Component.ConfDir, 0755); err != nil {
        return nil, fmt.Errorf("failed to make conf directory: %v", err)
    }
    if err := os.MkdirAll(c.Component.LogDir, 0755); err != nil {
        return nil, fmt.Errorf("failed to make log directory: %v", err)
    }
    if err := c.start(); err != nil {
        return nil, err
    }
    if err := c.readPortNumbers(); err != nil {
        return nil, err
    }
    return c, nil
}

func (c *ControlNode) start() error {
    if _, err := os.Stat(controlNodeBinary); os.IsNotExist(err) {
        log.Fatal(err)
    }

    if c.ConfFile == "" {
        c.ConfFile = controlNodeConfFile
    }

    c.Cmd = exec.Command(controlNodeBinary, "--config_file=" + c.Component.ConfFile)
    env := sut.EnvMap{
        "USER": os.Getenv("USER"),
        "BGP_IFMAP_XMPP_INTEGRATION_TEST_SELF_NAME": c.Name,
        "CAT_BGP_IP_ADDRESS": c.IPAddress,
        "CAT_BGP_PORT": strconv.Itoa(c.Config.BGPPort),
        "CAT_XMPP_PORT": strconv.Itoa(c.Config.XMPPPort),
        "BGP_IFMAP_XMPP_INTEGRATION_TEST_INTROSPECT": strconv.Itoa(c.Config.HTTPPort),
        "BGP_IFMAP_XMPP_INTEGRATION_TEST_PAUSE": "1",
//      "LOG_DISABLE" : strconv.FormatBool(c.Verbose),
        "BGP_IFMAP_XMPP_INTEGRATION_TEST_DATA_FILE": c.ConfFile,
        "LD_LIBRARY_PATH": "../../../../build/lib",
        "CONTRAIL_CAT_FRAMEWORK": "1",
        "USER_DIR": c.ConfDir + "/..",
    }
    c.Cmd.Env = c.Env(env)
    if err := c.Cmd.Start(); err != nil {
        return fmt.Errorf("Failed to start agent: %v", err)
    }
    return nil
}

func (c *ControlNode) Restart() error {
    log.Infof("Restarted %s\n", c.Name)
    if err := c.Stop(); err != nil {
        return err
    }
    return c.start()
}

func (c *ControlNode) readPortNumbers() error {
    c.PortsFile = fmt.Sprintf("%s/%d.json", c.ConfDir, c.Cmd.Process.Pid)
    retry := 30
    var err error
    for retry > 0 {
        if bytes, err := ioutil.ReadFile(c.PortsFile); err == nil {
            if err := json.Unmarshal(bytes, &c.Config); err == nil {
                return nil
            }
        }
        time.Sleep(1 * time.Second)
        retry = retry - 1
    }
    return err
}

func (c *ControlNode) CheckXMPPConnection(agent *agent.Agent) error {
    url := fmt.Sprintf("/usr/bin/curl -s http://127.0.0.1:%d/Snh_BgpNeighborReq?x=%s | xmllint --format  - | grep -i state | grep -i Established", c.Config.HTTPPort,  agent.Component.Name)
    _, err := exec.Command("/bin/bash", "-c", url).Output()
    return err
}

func (c *ControlNode) CheckXMPPConnections(agents []*agent.Agent, retry int, wait time.Duration) error {
    for r := 0; r < retry; r++ {
        var err error
        for i := 0; i < len(agents); i++ {
            if err = c.CheckXMPPConnection(agents[i]); err != nil {
                break
            }
        }
        if err == nil {
            return nil
        }
        time.Sleep(wait * time.Second)
    }
    return fmt.Errorf("CheckXMPPConnections failed")
}

func (c *ControlNode) CheckBGPConnection(control_node *ControlNode) error {
    url := fmt.Sprintf("/usr/bin/curl -s http://127.0.0.1:%d/Snh_BgpNeighborReq?x=%s | xmllint --format  - | grep -i state | grep -i Established", c.Config.HTTPPort,  control_node.Component.Name)
    _, err := exec.Command("/bin/bash", "-c", url).Output()
    return err
}

func (c *ControlNode) CheckBGPConnections(control_nodes []*ControlNode, retry int, wait time.Duration) error {
    for r := 0; r < retry; r++ {
        var err error
        for i := 0; i < len(control_nodes); i++ {
            if c != control_nodes[i] {
                if err = c.CheckBGPConnection(control_nodes[i]); err != nil {
                    break
                }
            }
        }
        if err == nil {
            return nil
        }
        time.Sleep(wait * time.Second)
    }
    return fmt.Errorf("CheckBGPConnections failed")
}

// TODO: Parse xml to do specific checks in the data received from curl.
func (c *ControlNode) checkConfiguration(tp string, count int) error {
    url := fmt.Sprintf("/usr/bin/curl -s http://127.0.0.1:%d/Snh_IFMapNodeTableListShowReq? | xmllint format -  | grep -iw -A1 '>%s</table_name>' | grep -w '>'%d'</size>'", c.Config.HTTPPort, tp, count)
    _, err := exec.Command("/bin/bash", "-c", url).Output()
    return err
}

func (c *ControlNode) CheckConfiguration(tp string, count int, retry int, wait time.Duration) error {
    for r := 0; r < retry; r++ {
        if err := c.checkConfiguration(tp, count); err == nil {
            return nil
        }
        time.Sleep(wait * time.Second)
    }
    return fmt.Errorf("CheckConfiguration failed")
}
