Por el momento estos deployments son con localhost, para kafka y rabbit se tiene que cambiar eso por el servicio que corresponde, "modulo: proyecto2"

## Comandos usados  

Generar el código Go con protoc
```bash
export PATH=$PATH:~/go/bin
protoc --go_out=. --go_opt=paths=source_relative   --go-grpc_out=. --go-grpc_opt=paths=source_relative   proto/writer.proto
```

carpetas client, writer_kafka, writer_rabbit (acceder a cada una para ejecución)

```bash
go run main.go
```

## Instalaciones

Se instalo una libreria externa "protocol buffers v30.2"
https://github.com/protocolbuffers/protobuf/releases  

Guia instalación
https://x.com/i/grok/share/Nwj3b8naBnlBxux8nNge67YbK  

Plogins de Go para protoc

```bash
go install google.golang.org/protobuf/cmd/protoc-gen-go@latest
go install google.golang.org/grpc/cmd/protoc-gen-go-grpc@latest
```
