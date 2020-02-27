package contrailcli

import (
	"strings"

	"github.com/Juniper/asf/pkg/logutil"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
)

var configFile string

func init() {
	cobra.OnInitialize(initConfig)
	ContrailCLI.PersistentFlags().StringVarP(&configFile, "config", "c", "", "Configuration file")
	viper.SetEnvPrefix("contrail")
	viper.SetEnvKeyReplacer(strings.NewReplacer(".", "_"))
	viper.AutomaticEnv()
}

// ContrailCLI defines root Contrail CLI command.
var ContrailCLI = &cobra.Command{
	Use:   "contrailcli",
	Short: "Contrail CLI command",
	Run:   func(cmd *cobra.Command, args []string) {},
}

func initConfig() {
	if configFile == "" {
		configFile = viper.GetString("config")
	}
	if configFile != "" {
		viper.SetConfigFile(configFile)
	}
	if err := viper.ReadInConfig(); err != nil {
		logutil.FatalWithStackTrace(err)
	}
}
