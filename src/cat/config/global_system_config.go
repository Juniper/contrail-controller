/*
 * copyright (c) 2019 juniper networks, inc. all rights reserved.
 */

package config


import (
    "encoding/json"
)

// GlobalSystemsConfig represents global-systems-config configuration construct.
type GlobalSystemsConfig struct {
    *ContrailConfig
    AutonomousSystem string `json:"prop:autonomous_system"`
}

func NewGlobalSystemsConfig(fqNameTable *FQNameTableType, uuidTable *UUIDTableType, as string) (*GlobalSystemsConfig, error) {
    co, err := createContrailConfig(fqNameTable, "global_system_config", "default-global-system-config", "", []string{"default-global-system-config"})
    if err != nil {
        return nil, err
    }
    o := &GlobalSystemsConfig{
        ContrailConfig: co,
        AutonomousSystem: as,
    }

    err = o.UpdateDB(uuidTable)
    if err != nil {
        return nil, err
    }
    return o, nil
}

func (o *GlobalSystemsConfig) UpdateDB(uuidTable *UUIDTableType) error {
    b, err := json.Marshal(o)
    if err != nil {
        return err
    }
    (*uuidTable)[o.UUID], err = o.ToJson(b)
    return err
}
