# asf
Repo for API Server Framework related work.

# Testing
Currently not all packages are compiling successfully, which prevents from using `go test ./...`.
To test packages that are ready, call:
```
make test
```

# Regenerating the code
After modyfying Protobuf definitions `*.pb.go` files need to be regenerated.
To regenerate the definitions call:
```
go generate ./...
```

The resulting files should be checked into the repository to allow library users import them as go library.
