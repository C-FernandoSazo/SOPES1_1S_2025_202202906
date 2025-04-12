package main

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"strings"
	"sync"
	"time"

	"github.com/redis/go-redis/v9"
	"github.com/segmentio/kafka-go"
)

type ClimaMessage struct {
	Description string `json:"description"`
	Country     string `json:"country"`
	Weather     string `json:"weather"`
}

func main() {
	// Configurar cliente Redis
	redisClient := redis.NewClient(&redis.Options{
		Addr: "redis.proyecto2.svc.cluster.local:6379", // Servicio de Redis en el namespace
	})
	ctx := context.Background()
	if err := redisClient.Ping(ctx).Err(); err != nil {
		log.Fatalf("Error conectando a Redis: %v", err)
	}
	log.Println("Conectado a Redis")

	// Configurar lector Kafka con GroupID para balanceo
	reader := kafka.NewReader(kafka.ReaderConfig{
		Brokers:  []string{"my-cluster-kafka-bootstrap.proyecto2.svc.cluster.local:9092"},
		Topic:    "clima-topic",
		GroupID:  "clima-consumer-group",
		MinBytes: 10e3, // 10KB
		MaxBytes: 10e6, // 10MB
	})
	defer reader.Close()

	log.Println("Consumidor Kafka iniciado")

	// Usar WaitGroup para manejar goroutines
	var wg sync.WaitGroup

	// Canal para procesar mensajes
	msgChan := make(chan kafka.Message, 100)

	// Lanzar 4 goroutines para procesar mensajes en paralelo
	for i := 0; i < 4; i++ {
		wg.Add(1)
		go func(workerID int) {
			defer wg.Done()
			for msg := range msgChan {
				processMessage(ctx, redisClient, msg, workerID)
			}
		}(i)
	}

	// Leer mensajes de Kafka y enviarlos al canal
	for {
		msg, err := reader.ReadMessage(ctx)
		if err != nil {
			log.Printf("Error leyendo mensaje: %v", err)
			continue
		}
		msgChan <- msg
	}
}

func processMessage(ctx context.Context, redisClient *redis.Client, msg kafka.Message, workerID int) {
	var clima ClimaMessage
	if err := json.Unmarshal(msg.Value, &clima); err != nil {
		log.Printf("Worker %d: Error parseando JSON: %v", workerID, err)
		return
	}

	// Generar clave única con partición y offset para evitar duplicación
	key := fmt.Sprintf("clima:%s:%d-%d", strings.ReplaceAll(clima.Country, " ", "_"), msg.Partition, msg.Offset)

	// Verificar si el mensaje ya fue procesado (evitar duplicación)
	if exists, _ := redisClient.Exists(ctx, key).Result(); exists > 0 {
		log.Printf("Worker %d: Mensaje ya procesado (clave: %s)", workerID, key)
		return
	}

	// Almacenar en Redis como hash
	err := redisClient.HSet(ctx, key, map[string]interface{}{
		"description": clima.Description,
		"country":     clima.Country,
		"weather":     clima.Weather,
	}).Err()
	if err != nil {
		log.Printf("Worker %d: Error almacenando en Redis: %v", workerID, err)
		return
	}

	// Establecer TTL de 7 días
	redisClient.Expire(ctx, key, 7*24*time.Hour)
	log.Printf("Worker %d: Mensaje procesado y almacenado: %s (clave: %s)", workerID, msg.Value, key)
}
