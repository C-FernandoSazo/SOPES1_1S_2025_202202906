package main

import (
	"context"
	"fmt"
	"log"
	"net"

	pb "proyecto2/proto"

	amqp "github.com/rabbitmq/amqp091-go"

	"google.golang.org/grpc"
)

type server struct {
	pb.UnimplementedWriterServiceServer
	conn *amqp.Connection
	ch   *amqp.Channel
}

func main() {
	// Conectar a RabbitMQ
	conn, err := amqp.Dial("amqp://default_user_vPpH5kybkT4B7rsB_yp:VhcYNU8eBPN9TkbfyAjK9IUe2yIcrSba@clima-rabbit.proyecto2.svc.cluster.local:5672/")
	if err != nil {
		log.Fatalf("Error conectando a RabbitMQ: %v", err)
	}
	defer conn.Close()

	ch, err := conn.Channel()
	if err != nil {
		log.Fatalf("Error abriendo canal: %v", err)
	}
	defer ch.Close()

	// Declarar la cola
	_, err = ch.QueueDeclare(
		"clima-queue",
		true,  // durable
		false, // autoDelete
		false, // exclusive
		false, // noWait
		nil,
	)
	if err != nil {
		log.Fatalf("Error declarando cola: %v", err)
	}

	// Iniciar servidor gRPC
	lis, err := net.Listen("tcp", ":50051")
	if err != nil {
		log.Fatalf("Error iniciando servidor: %v", err)
	}

	s := grpc.NewServer()
	pb.RegisterWriterServiceServer(s, &server{conn: conn, ch: ch})
	log.Println("RabbitMQ gRPC Server corriendo en :50051")
	if err := s.Serve(lis); err != nil {
		log.Fatalf("Error sirviendo: %v", err)
	}
}

func (s *server) Publish(ctx context.Context, req *pb.PublishRequest) (*pb.PublishResponse, error) {
	body := fmt.Sprintf(`{"description":"%s","country":"%s","weather":"%s"}`,
		req.Description, req.Country, req.Weather)

	err := s.ch.PublishWithContext(
		ctx,
		"",            // exchange
		"clima-queue", // routing key
		false,         // mandatory
		false,         // immediate
		amqp.Publishing{
			ContentType: "application/json",
			Body:        []byte(body),
		},
	)
	if err != nil {
		return &pb.PublishResponse{
			Success: false,
			Info:    fmt.Sprintf("Error publicando: %v", err),
		}, err
	}

	log.Printf("[RabbitMQ] Publicado: %s", body)
	return &pb.PublishResponse{
		Success: true,
		Info:    "Publicado en RabbitMQ con éxito",
	}, nil
}
