package contrailutil

import (
	"github.com/spf13/cobra"
	"github.com/spf13/viper"

	//"github.com/Juniper/asf/pkg/constants"
	"github.com/Juniper/asf/pkg/etcd"
	"github.com/Juniper/asf/pkg/convert"
	"github.com/Juniper/asf/pkg/logutil"
)

func init() {
	ContrailUtil.AddCommand(convertCmd)
	convertCmd.Flags().StringVarP(&inType, "intype", "", "",
		`input type: "yaml" and "rdbms" are supported`)
	convertCmd.Flags().StringVarP(&inFile, "in", "i", "", "Input file")
	convertCmd.Flags().StringVarP(&outType, "outtype", "", "",
		`output type: "rdbms", "yaml", "etcd" and "http" are supported`)
	convertCmd.Flags().StringVarP(&outFile, "out", "o", "", "Output file")
	convertCmd.Flags().StringVarP(&url, "url", "u", "", `Endpoint URL for "http" output type.`)
}

var inType, inFile string
var outType, outFile string
var url string

var convertCmd = &cobra.Command{
	Use:   "convert",
	Short: "convert data format",
	Long:  `This command converts data formats from one to another`,
	Run: func(cmd *cobra.Command, args []string) {
		err := convert.Convert(&convert.Config{
			InType:                  inType,
			InFile:                  inFile,
			OutType:                 outType,
			OutFile:                 outFile,
			EtcdNotifierPath:        viper.GetString(etcd.ETCDEndpointsVK),
			URL:                     url,
		})
		if err != nil {
			logutil.FatalWithStackTrace(err)
		}
	},
}
