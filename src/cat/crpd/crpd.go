/*
 * copyright (c) 2019 juniper networks, inc. all rights reserved.
 */

package crpd

import (
    "cat/sut"
    "fmt"
    "github.com/google/uuid"
    "io"
    "os"
    "os/exec"
    "path/filepath"
    "strings"
    "time"
)

type CRPD struct {
    sut.Component
    UUID string
}

const crpdName = "crpd"
const dockerFile = "/.dockerenv"

func New(m sut.Manager, name, test string) (*CRPD, error) {
    u, err := uuid.NewUUID()
    if err != nil {
        return nil, err
    }
    n := fmt.Sprintf("%s-%s", name, u.String())
    cmd := exec.Command("sudo", "--non-interactive", "docker", "run", "--name", n, "-itd", "crpd")
    if err := cmd.Start(); err != nil {
        return nil, fmt.Errorf("Failed to start crpd docker container %v", err)
    }

    time.Sleep(1 * time.Second)

    cmd = exec.Command("sudo", "--non-interactive", "docker", "inspect", "-f", "{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}", n)
    ip_bytes, err := cmd.Output()
    if err != nil {
        return nil, fmt.Errorf("Failed find IP address of crpd container %v", err)
    }
    ip := strings.TrimRight(string(ip_bytes), "\n")

    // Apply initial configuration
    conf := `
configure
delete system
delete protocols bgp group test
set system root-authentication encrypted-password "$6$0uOJV$DeOUWubpQesgtIszCKmwGQwOFUS9YvcXuiFbzY52XLO.Gx4XJFWjJEp28vvtxqdYJyMaB3gkoVNsSUhpHvLxO/"
set routing-options autonomous-system 64512
set protocols bgp group test type internal
set protocols bgp group test allow 0.0.0.0/0 passive peer-as 64512
set protocols bgp group test family inet unicast
set protocols bgp group test family inet6 unicast
set protocols bgp group test family inet-vpn unicast
set protocols bgp group test family inet6-vpn unicast
set protocols bgp group test family evpn signaling
set protocols bgp group test family route-target
commit
`
    cmd = exec.Command("sudo", "--non-interactive", "docker", "exec", "-i", n, "cli")
    stdin, err := cmd.StdinPipe()
    if err != nil {
        return nil, fmt.Errorf("Failed to get stdin pipe docker exec command %v", err)
    }

    defer stdin.Close()
    io.WriteString(stdin, conf)

    if err := cmd.Start(); err != nil {
        return nil, fmt.Errorf("Failed to apply configuration to crpd container %v", err)
    }

    c := &CRPD{
        Component: sut.Component{
            Name:    n,
            IPAddress: ip,
            Manager: m,
            LogDir: filepath.Join(m.RootDir,test,crpdName, name, "log"),
            ConfDir: filepath.Join(m.RootDir,test,crpdName, name, "conf"),
        },
        UUID: u.String(),
    }
    return c, nil
}

func CanUseRpd() bool {
    // Can't run crpd container inside a container..
    if _, err := os.Stat(dockerFile); !os.IsNotExist(err) {
        return false
    }
    if _, err := exec.Command("sudo", "--non-interactive", "docker", "ps").Output(); err != nil {
        return false
    }
    return true
}

func (c *CRPD) Teardown() error {
    if c.Name == "" {
        return fmt.Errorf("Cannot find crpd name")
    }
    cmd := exec.Command("sudo", "docker", "rm", "-f", c.Name)
    if err := cmd.Start(); err != nil {
        return fmt.Errorf("Failed to remove crpd docker container %v", err)
    }
    return nil
}

func (c *CRPD) Stop() error {
    cmd := exec.Command("sudo", "--non-interactive", "docker", "stop", c.Name)
    if err := cmd.Start(); err != nil {
        return fmt.Errorf("Failed to stop crpd docker container %v", err)
    }
    return nil
}

func (c *CRPD) Restart() error {
    cmd := exec.Command("sudo", "--non-interactive", "docker", "restart", c.Name)
    if err := cmd.Start(); err != nil {
        return fmt.Errorf("Failed to restart crpd docker container %v", err)
    }
    return nil
}
