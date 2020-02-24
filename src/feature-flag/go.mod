module feature-flag

go 1.12

require (
	github.com/Juniper/asf v0.0.0-20200221120017-04f7bb1a813f
	github.com/Juniper/contrail-go-api v1.1.0
	github.com/labstack/echo v3.3.10+incompatible
	github.com/sirupsen/logrus v1.4.2
	github.com/spf13/cobra v0.0.6
	github.com/spf13/viper v1.6.2
)

replace github.com/Juniper/contrail-go-api => /home/ijohnson/gocode/src/github.com/cijohnson/contrail-feature-flag/vendor/github.com/Juniper/contrail-go-api
