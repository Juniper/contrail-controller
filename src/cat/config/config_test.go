
/*
 * copyright (c) 2019 juniper networks, inc. all rights reserved.
 */

package config_test

import (
    "cat/config"
    "fmt"
    log "github.com/sirupsen/logrus"
    "testing"
)

func verifySize (fqNameTable *config.FQNameTableType, uuidTable *config.UUIDTableType, fq_size int, uuid_size int) error {
    if len(*fqNameTable) != fq_size {
        return fmt.Errorf("Incorrect fqNameTable size %d; want %d", len(*fqNameTable), fq_size)
    }
    if len(*uuidTable) != uuid_size {
        return fmt.Errorf("Incorrect uuidTable size %d; want %d", len(*uuidTable), fq_size)
    }
    return nil
}

func Test1(t *testing.T) {
    fqNameTable := make(config.FQNameTableType)
    uuidTable := make(config.UUIDTableType)

    count := 0
    tp := 0

    for i := 0; i < 10; i++ {
        tgt := fmt.Sprintf("target:100:%d", i)
        _, err := config.NewConfigObject(&fqNameTable, &uuidTable, "route_target", tgt, "", []string{tgt}); if err != nil {
            t.Errorf("Cannot create config object: %v", err)
        }
    }
    count += 10
    tp++

    for i := 0; i < 10; i++ {
        name := fmt.Sprintf("ri%d", i)
        _, err := config.NewRoutingInstance(&fqNameTable, &uuidTable, name)
        if err != nil {
            t.Errorf("Cannot create routing-instance %s: %v", name, err)
        }
    }
    count += 10
    tp++

    for i := 0; i < 10; i++ {
        name := fmt.Sprintf("vn%d", i)
        _, err := config.NewVirtualNetwork(&fqNameTable, &uuidTable, name)
        if err != nil {
            t.Errorf("Cannot create virtual-network %s: %v", name, err)
        }
    }
    count += 10
    tp++

    for i := 0; i < 10; i++ {
        name := fmt.Sprintf("gsc%d", i)
        _, err := config.NewGlobalSystemsConfig(&fqNameTable, &uuidTable, name)
        if err != nil {
            t.Errorf("Cannot create global-systems-config %s: %v", name, err)
        }
    }
    count += 10
    tp++

    for i := 0; i < 10; i++ {
        name := fmt.Sprintf("bgp-router%d", i)
        address := fmt.Sprintf("127.0.0.%d", i)
        _, err := config.NewBGPRouter(&fqNameTable, &uuidTable, name, address, 0)
        if err != nil {
            t.Errorf("Cannot create bgp-router %s: %v", name, err)
        }
    }
    count += 10
    tp++

    for i := 0; i < 10; i++ {
        name := fmt.Sprintf("vr%d", i)
        address := fmt.Sprintf("127.0.0.%d", i)
        _, err := config.NewVirtualRouter(&fqNameTable, &uuidTable, name, address)
        if err != nil {
            t.Errorf("Cannot create virtual-router %s: %v", name, err)
        }
    }
    count += 10
    tp++

    for i := 0; i < 10; i++ {
        name := fmt.Sprintf("vmi%d", i)
        _, err := config.NewVirtualMachineInterface(&fqNameTable, &uuidTable, name)
        if err != nil {
            t.Errorf("Cannot create virtual-machine-interface %s: %v", name, err)
        }
    }
    count += 10
    tp++

    for i := 0; i < 10; i++ {
        name := fmt.Sprintf("ip%d", i)
        ip := fmt.Sprintf("1.2.3.%d", i)

        _, err := config.NewInstanceIp(&fqNameTable, &uuidTable, name, ip, "v4")
        if err != nil {
            t.Errorf("Cannot create instance-ip %s: %v", name, err)
        }
    }
    count += 10
    tp++

    err := verifySize(&fqNameTable, &uuidTable, tp, count); if err != nil {
        t.Errorf("FQName/UUID Table size is incorrect: %v", err)
    }

    log.Debugf("FQNameTable %v", fqNameTable)
    log.Debugf("UUIDTable %v", uuidTable)
}
