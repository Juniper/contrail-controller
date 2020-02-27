package contrailcli

import (
	"fmt"

	"github.com/Juniper/asf/pkg/client"
	"github.com/Juniper/asf/pkg/logutil"
	"github.com/spf13/cobra"
)

func init() {
	ContrailCLI.AddCommand(syncCmd)
}

var syncCmd = &cobra.Command{
	Use:   "sync [FilePath]",
	Short: "Synchronise resources with data defined in given YAML file",
	Long: `
Sync creates new resource for every not already existing resource
Use resource format just like in 'schema' command output`,
	Args: cobra.ExactArgs(1),
	Run: func(cmd *cobra.Command, args []string) {
		cli, err := client.NewCLIByViper()
		if err != nil {
			logutil.FatalWithStackTrace(err)
		}

		r, err := cli.SyncResources(args[0])
		if err != nil {
			logutil.FatalWithStackTrace(err)
		}

		fmt.Println(r)
	},
}
