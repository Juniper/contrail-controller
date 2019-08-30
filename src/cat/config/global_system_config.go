/*
 * copyright (c) 2019 juniper networks, inc. all rights reserved.
 */

package config


import (
    "encoding/json"
)

type GlobalSystemsConfig struct {
    *ContrailConfigObject
    AutonomousSystem string `json:"prop:autonomous_system"`
}

func NewGlobalSystemsConfig(as string) (*GlobalSystemsConfig, error) {
    co, err := createContrailConfigObject("global_system_config", "default-global-system-config", "", []string{"default-global-system-config"})
    if err != nil {
        return nil, err
    }
    o := &GlobalSystemsConfig{
        ContrailConfigObject: co,
        AutonomousSystem: as,
    }

    err = o.UpdateDB()
    if err != nil {
        return nil, err
    }
    return o, nil
}

func (o *GlobalSystemsConfig) UpdateDB() error {
    b, err := json.Marshal(o)
    if err != nil {
        return err
    }
    UUIDTable[o.Uuid], err = o.ToJson(b)
    return err
}
