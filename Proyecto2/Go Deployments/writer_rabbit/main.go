package main

import (
	"context"
	"fmt"
	"log"
	"net"

	pb "proyecto2/proto" // Ajusta import según tu módulo

	"google.golang.org/grpc"
)

// Estructura que implementa el servidor
type rabbitServer struct {
	pb.UnimplementedWriterServiceServer
}

// Método Publish, se llamará desde Deployment 1
func (s *rabbitServer) Publish(ctx context.Context, req *pb.PublishRequest) (*pb.PublishResponse, error) {
	// Aquí tu lógica para publicar en RabbitMQ
	// Ejemplo de "simulación":
	fmt.Printf("[Rabbit] Publicando: %s - %s - %s\n",
		req.Description, req.Country, req.Weather)

	// Supón que lograste publicar sin error
	return &pb.PublishResponse{
		Success: true,
		Info:    "Publicado en RabbitMQ con éxito",
	}, nil
}

func main() {
	lis, err := net.Listen("tcp", ":50051")
	if err != nil {
		log.Fatalf("Error al iniciar listener: %v", err)
	}
	log.Println("Rabbit gRPC Server corriendo en :50051")

	grpcServer := grpc.NewServer()
	pb.RegisterWriterServiceServer(grpcServer, &rabbitServer{})

	if err := grpcServer.Serve(lis); err != nil {
		log.Fatalf("Error al iniciar servidor gRPC: %v", err)
	}
}
