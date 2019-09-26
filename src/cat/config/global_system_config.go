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
	g := &GlobalSystemsConfig{
		ContrailConfig:   co,
		AutonomousSystem: as,
	}

	err = g.UpdateDB(uuidTable)
	if err != nil {
		return nil, err
	}
	return g, nil
}

func (g *GlobalSystemsConfig) UpdateDB(uuidTable *UUIDTableType) error {
	b, err := json.Marshal(g)
	if err != nil {
		return err
	}
	(*uuidTable)[g.UUID], err = g.Map(b)
	return err
}
