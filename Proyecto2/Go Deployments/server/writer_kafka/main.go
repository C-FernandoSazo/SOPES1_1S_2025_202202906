package main

import (
	"context"
	"fmt"
	"log"
	"net"

	pb "proyecto2/proto"

	"github.com/segmentio/kafka-go"

	"google.golang.org/grpc"
)

type kafkaServer struct {
	pb.UnimplementedWriterServiceServer
	writer *kafka.Writer
}

// Inicializar el escritor de Kafka
func newKafkaServer() *kafkaServer {
	writer := &kafka.Writer{
		Addr:     kafka.TCP("my-cluster-kafka-bootstrap.proyecto2.svc.cluster.local:9092"),
		Topic:    "clima-topic",
		Balancer: &kafka.LeastBytes{},
	}
	return &kafkaServer{writer: writer}
}

func (s *kafkaServer) Publish(ctx context.Context, req *pb.PublishRequest) (*pb.PublishResponse, error) {
	// Crear el mensaje para Kafka
	msg := kafka.Message{
		Value: []byte(fmt.Sprintf(`{"description":"%s","country":"%s","weather":"%s"}`,
			req.Description, req.Country, req.Weather)),
	}

	// Publicar en Kafka
	err := s.writer.WriteMessages(ctx, msg)
	if err != nil {
		log.Printf("Error publicando en Kafka: %v", err)
		return &pb.PublishResponse{
			Success: false,
			Info:    fmt.Sprintf("Error publicando en Kafka: %v", err),
		}, nil
	}

	log.Printf("[Kafka] Publicado: %s - %s - %s", req.Description, req.Country, req.Weather)

	return &pb.PublishResponse{
		Success: true,
		Info:    "Publicado en Kafka con éxito",
	}, nil
}

func (s *kafkaServer) Close() {
	if s.writer != nil {
		s.writer.Close()
	}
}

func main() {
	lis, err := net.Listen("tcp", ":50052")
	if err != nil {
		log.Fatalf("Error al iniciar listener: %v", err)
	}
	log.Println("Kafka gRPC Server corriendo en :50052")

	server := newKafkaServer()
	defer server.Close()

	grpcServer := grpc.NewServer()
	pb.RegisterWriterServiceServer(grpcServer, server)

	if err := grpcServer.Serve(lis); err != nil {
		log.Fatalf("Error al iniciar servidor gRPC: %v", err)
	}
}
