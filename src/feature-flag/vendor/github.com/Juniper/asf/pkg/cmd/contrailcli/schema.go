package contrailcli

import (
	"fmt"

	"github.com/Juniper/asf/pkg/client"
	"github.com/Juniper/asf/pkg/logutil"
	"github.com/spf13/cobra"
)

func init() {
	ContrailCLI.AddCommand(schemaCmd)
}

var schemaCmd = &cobra.Command{
	Use:   "schema [SchemaID]",
	Short: "Show schema for specified resource",
	Args:  cobra.ExactArgs(1),
	Run: func(cmd *cobra.Command, args []string) {
		schemaID := ""
		if len(args[0]) > 0 {
			schemaID = args[0]
		}

		cli, err := client.NewCLIByViper()
		if err != nil {
			logutil.FatalWithStackTrace(err)
		}

		r, err := cli.ShowSchema(schemaID)
		if err != nil {
			logutil.FatalWithStackTrace(err)
		}

		fmt.Println(r)
	},
}
