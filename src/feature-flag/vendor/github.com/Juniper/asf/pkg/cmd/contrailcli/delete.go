package contrailcli

import (
	"fmt"

	"github.com/Juniper/asf/pkg/client"
	"github.com/Juniper/asf/pkg/logutil"
	"github.com/spf13/cobra"
)

func init() {
	ContrailCLI.AddCommand(deleteCmd)
}

var deleteCmd = &cobra.Command{
	Use:   "delete [FilePath]",
	Short: "Delete resources specified in given YAML file",
	Long:  "Use resource format just like in 'schema' command output",
	Args:  cobra.ExactArgs(1),
	Run: func(cmd *cobra.Command, args []string) {
		cli, err := client.NewCLIByViper()
		if err != nil {
			logutil.FatalWithStackTrace(err)
		}

		r, err := cli.DeleteResources(args[0])
		if err != nil {
			logutil.FatalWithStackTrace(err)
		}

		fmt.Println(r)
	},
}
