package contrailcli

import (
	"fmt"

	"github.com/Juniper/asf/pkg/client"
	"github.com/Juniper/asf/pkg/logutil"
	"github.com/spf13/cobra"
)

func init() {
	ContrailCLI.AddCommand(rmCmd)
}

var rmCmd = &cobra.Command{
	Use:   "rm [SchemaID] [UUID]",
	Short: "Remove a resource with specified UUID",
	Long:  "Invoke command with empty SchemaID in order to show possible usages",
	Run: func(cmd *cobra.Command, args []string) {
		schemaID, uuid := "", ""
		if len(args) >= 2 {
			schemaID, uuid = args[0], args[1]
		}

		cli, err := client.NewCLIByViper()
		if err != nil {
			logutil.FatalWithStackTrace(err)
		}

		r, err := cli.DeleteResource(schemaID, uuid)
		if err != nil {
			logutil.FatalWithStackTrace(err)
		}

		fmt.Println(r)
	},
}
