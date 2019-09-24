// package sut (sytem under test) prodes basic utility methods to manage the
// system under test.
package sut

import (
    "fmt"
    "os"
    "os/exec"
    "strings"
    "syscall"
    "time"
)

// Manager represents the top level object in CAT framework and holds data that is common tos several other components. Eventually these needs to be tunable from config or command line options.
type Manager struct {
    Verbose   bool
    IP        string
    RootDir   string
    ReportDir string
}

// Component represents a single unix process such as control-node and agent underneath. Hence it holds path to configuration files, log files, etc. and also the associated running os/exec.Cmd object.
type Component struct {
    Name      string
    IPAddress string
    Manager   Manager
    LogDir    string
    ConfDir   string
    ConfFile  string
    PortsFile string
    Verbose   bool
    Config    Config
    Cmd      *exec.Cmd
}

// Endpoint denotes a tcp endpoint.
type Endpoint struct {
    IP string
    Port int
}

// Config denotes server port numbers set of different services provided by
// a component such as control-node.
type Config struct {
    BGPPort  int `json:BGPPort`
    XMPPPort int `json:XMPPPort`
    HTTPPort int `json:HTTPPort`
}

// EnvMap is a map to denot set of environment variables and values.
type EnvMap map[string]string

// ShellCommandWithRetry runs a command in shell repeatedly as desired until
// success and return the output. Use retry < 1 to retry the command for ever..
func ShellCommandWithRetry (retry, delay int, name string, args ...string) (string, error) {
    var err error
    var result_bytes []byte

    for {
        cmd := exec.Command(name, args...)
        if result_bytes, err = cmd.Output(); err == nil {
            return strings.TrimRight(string(result_bytes), "\n"), nil
        }

        if retry > 0 {
            retry -= 1
        } else {
            time.Sleep(time.Duration(delay) * time.Second)
            continue
        }

        if retry == 0 {
            break
        }
        time.Sleep(time.Duration(delay) * time.Second)
    }
    return "", fmt.Errorf("Failed to execute command %s: %v", name, err)
}

// removeDir removes a directory recursively.
func removeDir(dir string) error {
    _, err := os.Stat(dir); if !os.IsNotExist(err) {
        err := os.RemoveAll(dir); if err != nil {
            return err
        }
    }
    return nil
}

// Teardown does the necessary cleanup by removing conf and log directories.
func (c *Component) Teardown() error {
    c.Stop()
    if err := removeDir(c.ConfDir); err != nil {
        return err
    }
    if err := removeDir(c.LogDir); err != nil {
        return err
    }
    return nil
}

// Stop stops a component by terminating associated process.
func (c *Component) Stop() error {
    if err := c.Cmd.Process.Signal(syscall.SIGTERM); err != nil {
        return fmt.Errorf("Could not stop process %d", c.Cmd.Process.Pid)
    }
    file := fmt.Sprintf("%d.json", c.Cmd.Process.Pid)
    _, err := os.Stat(file)
    if !os.IsNotExist(err) {
        if err := os.Remove(file); err != nil {
            return err
        }
    }
    return nil
}

// Env updates environment variable map.
func (a *Component) Env(e EnvMap) []string {
    var envs []string
    for k, v := range e {
        envs = append(envs, k + "=" + v)
    }
    return envs
}
