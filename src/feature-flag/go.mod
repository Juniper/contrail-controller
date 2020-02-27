module feature-flag

go 1.12

require (
	github.com/Juniper/asf v0.0.0-20200221120017-04f7bb1a813
	github.com/Juniper/contrail-go-api v1.1.0
	github.com/labstack/echo v3.3.10+incompatible
	github.com/spf13/cobra v0.0.6
	github.com/spf13/viper v1.6.2
)

// TODO(ijohnson): Remove replace of asf, once the asf repo is made public
//                 also vendor directory needs to be removed.
replace github.com/Juniper/asf v0.0.0-20200221120017-04f7bb1a813 => ./vendor/github.com/Juniper/asf
