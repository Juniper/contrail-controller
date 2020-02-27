package main

import (
	"strings"

	"feature-flag/pkg/feature"
	"github.com/Juniper/asf/pkg/logutil"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"

	asfapiserver "github.com/Juniper/asf/pkg/apiserver"
)

var configFile string
var enable []string
var disable []string

func init() {
	cobra.OnInitialize(initConfig)
}

func newRootCmd() *cobra.Command {
	rootCmd := &cobra.Command{
		Use:   "contrailflcm",
		Short: "Contrail feature flag life cycle managment command",
		Long:  ``,
		Run: func(cmd *cobra.Command, args []string) {
			run()
		},
	}
	rootCmd.PersistentFlags().StringVarP(&configFile, "conf", "c", "", "Feature flag configuration file")
	rootCmd.PersistentFlags().StringSliceVarP(&enable, "enable", "e", []string{}, "List of feature to enable")
	rootCmd.PersistentFlags().StringSliceVarP(&disable, "disable", "d", []string{}, "List of feature to disable")

	viper.SetEnvPrefix("contrailflcm")
	viper.SetEnvKeyReplacer(strings.NewReplacer(".", "_"))
	viper.AutomaticEnv()

	return rootCmd
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

	if err := logutil.Configure(viper.GetString("log_level")); err != nil {
		logutil.FatalWithStackTrace(err)
	}
}

func run() {
	feature.Manage()
	plugins := []asfapiserver.APIPlugin{
		feature.FeatureFlagAPIPlugin{},
	}

	server, err := asfapiserver.NewServer(plugins, []string{})
	logutil.FatalWithStackTraceIfError(err)

	err = server.Run()
	logutil.FatalWithStackTraceIfError(err)

}

func main() {
	if err := newRootCmd().Execute(); err != nil {
		logutil.FatalWithStackTrace(err)
	}

}
