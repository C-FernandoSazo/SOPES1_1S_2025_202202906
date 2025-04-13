package main

import (
	"log"

	amqp "github.com/rabbitmq/amqp091-go"
)

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
	q, err := ch.QueueDeclare(
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

	// Consumir mensajes
	msgs, err := ch.Consume(
		q.Name, // queue
		"",     // consumer tag
		true,   // auto-ack
		false,  // exclusive
		false,  // no-local
		false,  // no-wait
		nil,    // args
	)
	if err != nil {
		log.Fatalf("Error registrando consumidor: %v", err)
	}

	log.Println("Consumidor RabbitMQ iniciado, esperando mensajes...")

	// Procesar mensajes
	for msg := range msgs {
		log.Printf("Mensaje recibido: %s", msg.Body)
	}
}
