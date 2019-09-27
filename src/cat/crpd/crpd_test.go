// Package crpd_test provides unit tests to test crpd package methods.
package crpd_test

import (
	"os/exec"
	"strings"
	"testing"
	"time"

	"cat"
	"cat/crpd"
	"cat/sut"
)

func TestCRPD(t *testing.T) {
	if !crpd.CanUseCRPD() {
		return
	}
	c, err := cat.New()
	if err != nil {
		t.Errorf("Failed to create CAT object: %v", err)
	}
	cr, err := c.AddCRPD("test-crpd", "test-crpd1")
	if err != nil {
		t.Errorf("Failed to create crpd object: %v", err)
	}
	if !strings.HasPrefix(cr.Name, "test-crpd1-") {
		t.Errorf("incorrect crpd name %s; want %s", cr.Name, "test-crpd1")
	}

	// Verify that crpd container started and obtained an IP address.
	if cr.IPAddress == "" {
		t.Errorf("Cannot find crpd %s's ip address", cr.Name)
	}

	// Verify that container gets correct configuration.
	expectedConf := `set version "20190913.184434_rbu-builder.r1055445 [rbu-builder]"
set system root-authentication encrypted-password "$6$0uOJV$DeOUWubpQesgtIszCKmwGQwOFUS9YvcXuiFbzY52XLO.Gx4XJFWjJEp28vvtxqdYJyMaB3gkoVNsSUhpHvLxO/"
set routing-options autonomous-system 64512
set protocols bgp group test type internal
set protocols bgp group test passive
set protocols bgp group test family inet unicast
set protocols bgp group test family inet-vpn unicast
set protocols bgp group test family inet6 unicast
set protocols bgp group test family inet6-vpn unicast
set protocols bgp group test family evpn signaling
set protocols bgp group test family route-target
set protocols bgp group test peer-as 64512
set protocols bgp group test allow 0.0.0.0/0`

	for i := 0; i < 3; i++ {
		retrievedConf, err := sut.ShellCommandWithRetry(3, 3, "sudo", "--non-interactive", "docker", "exec", "-i", cr.Name, "cli", "show", "configuration", "|", "display", "set")

		if err != nil {
			t.Errorf("Cannot get crpd confiugration: %v", err)
		}

		if retrievedConf == expectedConf {
			break
		}

		if i == 2 {
			t.Fatalf("Expected configuration not found. Retrieved\n%s;\nwant\n%s", retrievedConf, expectedConf)
		}
		time.Sleep(3 * time.Second)
	}
	if err := cr.Teardown(); err != nil {
		t.Fatalf("crpd object cleanup failed: %v", err)
	}

	// Verify that docker process does not exist any more.
	if _, err := exec.Command("sudo", "--non-interactive", "docker", "inspect", cr.Name).Output(); err != nil {
		t.Fatalf("crpd docker cleanup failed: %v", err)
	}
}
