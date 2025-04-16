package main

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"strings"
	"time"

	amqp "github.com/rabbitmq/amqp091-go"
	"github.com/valkey-io/valkey-go"
)

type ClimaMessage struct {
	Description string `json:"description"`
	Country     string `json:"country"`
	Weather     string `json:"weather"`
}

func main() {
	// Conectar a Valkey
	client, err := valkey.NewClient(valkey.ClientOption{
		InitAddress: []string{"valkey.proyecto2.svc.cluster.local:6379"},
	})
	if err != nil {
		log.Fatalf("Error conectando a Valkey: %v", err)
	}
	defer client.Close()
	log.Println("Conectado a Valkey")

	// Conectar a RabbitMQ
	conn, err := amqp.Dial("amqp://default_user_OHcC7hSMN9bwkYmR9gc:EFGBxZb_RpQ8I8vjeDMUETWZHJnoPU-w@clima-rabbit.proyecto2.svc.cluster.local:5672/")
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
	q, err := ch.QueueDeclare(
		"clima-queue",
		true,
		false,
		false,
		false,
		nil,
	)
	if err != nil {
		log.Fatalf("Error declarando cola: %v", err)
	}

	// Consumir mensajes
	msgs, err := ch.Consume(
		q.Name,
		"",
		true,
		false,
		false,
		false,
		nil,
	)
	if err != nil {
		log.Fatalf("Error registrando consumidor: %v", err)
	}

	log.Println("Consumidor RabbitMQ iniciado")

	ctx := context.Background()
	for msg := range msgs {
		var clima ClimaMessage
		if err := json.Unmarshal(msg.Body, &clima); err != nil {
			log.Printf("Error parseando JSON: %v", err)
			continue
		}

		// Generar clave única
		key := fmt.Sprintf("clima:%s:%d", strings.ReplaceAll(clima.Country, " ", "_"), time.Now().UnixNano())

		// Almacenar en Valkey
		err = client.Do(ctx, client.B().Hset().Key(key).
			FieldValue().
			FieldValue("description", clima.Description).
			FieldValue("country", clima.Country).
			FieldValue("weather", clima.Weather).
			Build()).Error()
		if err != nil {
			log.Printf("Error almacenando en Valkey: %v", err)
			continue
		}

		// Establecer expiración (7 días)
		client.Do(ctx, client.B().Expire().Key(key).Seconds(7*24*3600).Build())

		log.Printf("Almacenado en Valkey: %s (clave: %s)", msg.Body, key)
	}
}
