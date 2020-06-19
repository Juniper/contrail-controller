// Package config_test provides unit tests to test config package methods.
package config_test

import (
	"fmt"
	"testing"

	"cat/config"
)

func verifySize(fqNameTable *config.FQNameTableType, uuidTable *config.UUIDTableType, fqSize int, uuidSize int) error {
	if len(*fqNameTable) != fqSize {
		return fmt.Errorf("Incorrect fqNameTable size %d; want %d", len(*fqNameTable), fqSize)
	}
	if len(*uuidTable) != uuidSize {
		return fmt.Errorf("Incorrect uuidTable size %d; want %d", len(*uuidTable), fqSize)
	}
	return nil
}

func TestConfig(t *testing.T) {
	fqNameTable := config.FQNameTableType{}
	uuidTable := config.UUIDTableType{}

	count := 0
	tp := 0

	for i := 0; i < 10; i++ {
		tgt := fmt.Sprintf("target:100:%d", i)
		if _, err := config.NewConfigObject(&fqNameTable, &uuidTable, "route_target", tgt, "", []string{tgt}); err != nil {
			t.Errorf("Cannot create config object: %v", err)
		}
	}
	count += 10
	tp++

	for i := 0; i < 10; i++ {
		name := fmt.Sprintf("ri%d", i)
		if _, err := config.NewRoutingInstance(&fqNameTable, &uuidTable, name); err != nil {
			t.Errorf("Cannot create routing-instance %s: %v", name, err)
		}
	}
	count += 10
	tp++

	for i := 0; i < 10; i++ {
		name := fmt.Sprintf("vn%d", i)
		if _, err := config.NewVirtualNetwork(&fqNameTable, &uuidTable, name); err != nil {
			t.Errorf("Cannot create virtual-network %s: %v", name, err)
		}
	}
	count += 10
	tp++

	for i := 0; i < 10; i++ {
		name := fmt.Sprintf("gsc%d", i)
		if _, err := config.NewGlobalSystemsConfig(&fqNameTable, &uuidTable, name); err != nil {
			t.Errorf("Cannot create global-systems-config %s: %v", name, err)
		}
	}
	count += 10
	tp++

	for i := 0; i < 10; i++ {
		name := fmt.Sprintf("bgp-router%d", i)
		address := fmt.Sprintf("127.0.0.%d", i)
		if _, err := config.NewBGPRouter(&fqNameTable, &uuidTable, name, address, "control-node", 0); err != nil {
			t.Errorf("Cannot create bgp-router %s: %v", name, err)
		}
	}
	count += 10
	tp++

	for i := 0; i < 10; i++ {
		name := fmt.Sprintf("vr%d", i)
		address := fmt.Sprintf("127.0.0.%d", i)
		if _, err := config.NewVirtualRouter(&fqNameTable, &uuidTable, name, address); err != nil {
			t.Errorf("Cannot create virtual-router %s: %v", name, err)
		}
	}
	count += 10
	tp++

	for i := 0; i < 10; i++ {
		name := fmt.Sprintf("vmi%d", i)
		if _, err := config.NewVirtualMachineInterface(&fqNameTable, &uuidTable, name, ""); err != nil {
			t.Errorf("Cannot create virtual-machine-interface %s: %v", name, err)
		}
	}
	count += 10
	tp++

	for i := 0; i < 10; i++ {
		name := fmt.Sprintf("ip%d", i)
		ip := fmt.Sprintf("1.2.3.%d", i)

		if _, err := config.NewInstanceIp(&fqNameTable, &uuidTable, name, ip, "v4"); err != nil {
			t.Errorf("Cannot create instance-ip %s: %v", name, err)
		}
	}
	count += 10
	tp++

	if err := verifySize(&fqNameTable, &uuidTable, tp, count); err != nil {
		t.Errorf("FQName/UUID Table size is incorrect: %v", err)
	}
}
