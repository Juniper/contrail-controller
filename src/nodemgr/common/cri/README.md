# CRI protobuf protocol for python

Version 1.18, available from [here](https://github.com/kubernetes-sigs/cri-tools/tree/master/vendor/k8s.io/cri-api/pkg/apis/runtime/v1alpha2)
python2 build requirements are:
* RPMs: python-devel.x86_64 python2-pip.noarch
* pip2: protobuf grpc gevent grpcio pbtc


``` run these to generate python code:
python -m grpc_tools.protoc -I. --python_out=. gogo.proto
python -m grpc_tools.protoc -I. --python_out=. --grpc_python_out=. api.proto
```
