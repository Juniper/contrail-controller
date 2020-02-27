package contrailschema

import (
	"github.com/spf13/cobra"
)

// ContrailSchema defines root Contrail utility command.
var ContrailSchema = &cobra.Command{
	Use:   "contrailschema",
	Short: "Contrail Schema Command",
	Run: func(cmd *cobra.Command, args []string) {
	},
}
