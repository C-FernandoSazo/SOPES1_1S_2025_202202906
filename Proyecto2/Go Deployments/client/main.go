package main

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"net/http"

	pb "proyecto2/proto"

	"google.golang.org/grpc"
)

var (
	// Variables de conexión a Rabbit y Kafka:
	rabbitAddr = "rabbit_writer:50051" // o la IP/Service real en tu cluster K8s
	kafkaAddr  = "kafka_writer:50052"  // SE CAMBIA PORQUE ES PARA PRUEBAS LOCALES
)

// Estructura para parsear JSON del body HTTP
type ClimaBody struct {
	Description string `json:"description"`
	Country     string `json:"country"`
	Weather     string `json:"weather"`
}

func main() {
	http.HandleFunc("/publicar", publicarHandler)

	log.Println("API REST gRPC Client corriendo en :8080")
	log.Fatal(http.ListenAndServe(":8080", nil))
}

func publicarHandler(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "Método no soportado", http.StatusMethodNotAllowed)
		return
	}

	// Parsear JSON { "description": "...", "country": "...", "weather": "..." }
	var data ClimaBody
	if err := json.NewDecoder(r.Body).Decode(&data); err != nil {
		http.Error(w, "JSON inválido", http.StatusBadRequest)
		return
	}

	// Llamar a Rabbit
	respRabbit, err := callWriterService(rabbitAddr, data)
	if err != nil {
		http.Error(w, "Error publicando en Rabbit: "+err.Error(), http.StatusInternalServerError)
		return
	}

	// Llamar a Kafka
	respKafka, err := callWriterService(kafkaAddr, data)
	if err != nil {
		http.Error(w, "Error publicando en Kafka: "+err.Error(), http.StatusInternalServerError)
		return
	}

	// Responder al cliente con un JSON de estado
	response := map[string]interface{}{
		"rabbit": respRabbit,
		"kafka":  respKafka,
	}
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(response)
}

// callWriterService se conecta a un servidor gRPC, llama Publish
func callWriterService(addr string, data ClimaBody) (*pb.PublishResponse, error) {
	// Conectar a gRPC (Insecure para simplificar. En producción usar TLS)
	conn, err := grpc.Dial(addr, grpc.WithInsecure())
	if err != nil {
		return nil, fmt.Errorf("Dial failed: %w", err)
	}
	defer conn.Close()

	client := pb.NewWriterServiceClient(conn)

	// Construir la request
	req := &pb.PublishRequest{
		Description: data.Description,
		Country:     data.Country,
		Weather:     data.Weather,
	}

	// Llamar al método Publish
	resp, err := client.Publish(context.Background(), req)
	if err != nil {
		return nil, fmt.Errorf("Publish RPC failed: %w", err)
	}

	return resp, nil
}
