package main

import (
	"context"
	"encoding/json"
	"log"
	"sync"

	"github.com/segmentio/kafka-go"
)

type ClimaMessage struct {
	Description string `json:"description"`
	Country     string `json:"country"`
	Weather     string `json:"weather"`
}

func main() {
	// Configurar lector Kafka con GroupID para balanceo entre réplicas
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
				processMessage(msg, workerID)
			}
		}(i)
	}

	// Leer mensajes de Kafka y enviarlos al canal
	ctx := context.Background()
	for {
		msg, err := reader.ReadMessage(ctx)
		if err != nil {
			log.Printf("Error leyendo mensaje: %v", err)
			continue
		}
		msgChan <- msg
	}
}

func processMessage(msg kafka.Message, workerID int) {
	var clima ClimaMessage
	if err := json.Unmarshal(msg.Value, &clima); err != nil {
		log.Printf("Worker %d: Error parseando JSON: %v", workerID, err)
		return
	}
	log.Printf("Worker %d: Mensaje recibido - Description: %s, Country: %s, Weather: %s",
		workerID, clima.Description, clima.Country, clima.Weather)
}
