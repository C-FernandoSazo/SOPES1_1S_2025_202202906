package main

import (
	"context"
	"fmt"
	"log"
	"net"

	pb "proyecto2/proto"

	"google.golang.org/grpc"
)

type kafkaServer struct {
	pb.UnimplementedWriterServiceServer
}

func (s *kafkaServer) Publish(ctx context.Context, req *pb.PublishRequest) (*pb.PublishResponse, error) {
	// Aquí tu lógica para publicar en Kafka
	fmt.Printf("[Kafka] Publicando: %s - %s - %s\n",
		req.Description, req.Country, req.Weather)

	return &pb.PublishResponse{
		Success: true,
		Info:    "Publicado en Kafka con éxito",
	}, nil
}

func main() {
	lis, err := net.Listen("tcp", ":50052")
	if err != nil {
		log.Fatalf("Error al iniciar listener: %v", err)
	}
	log.Println("Kafka gRPC Server corriendo en :50052")

	grpcServer := grpc.NewServer()
	pb.RegisterWriterServiceServer(grpcServer, &kafkaServer{})

	if err := grpcServer.Serve(lis); err != nil {
		log.Fatalf("Error al iniciar servidor gRPC: %v", err)
	}
}
