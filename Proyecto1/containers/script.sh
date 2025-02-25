#!/bin/bash

# Definir las opciones de consumo
OPTIONS=(
    "--cpu 2 --timeout 60s"       # CPU
    "--io 2 --timeout 60s"        # I/O
    "--vm 2 --vm-bytes 256M --timeout 60s"  # RAM
    "--hdd 2 --hdd-bytes 1G --timeout 60s"  # Disco
)

# Crear 10 contenedores con nombres únicos
for i in {1..10}; do
    # Seleccionar un tipo de contenedor aleatoriamente
    RANDOM_INDEX=$((RANDOM % 4))
    OPTION="${OPTIONS[$RANDOM_INDEX]}"
    
    # Generar un nombre único basado en la fecha y /dev/urandom
    CONTAINER_NAME="stress_$(date +%s)_$(head /dev/urandom | tr -dc A-Za-z0-9 | head -c 5)"

    # Ejecutar el contenedor en segundo plano
    docker run -d --name "$CONTAINER_NAME" containerstack/alpine-stress stress $OPTION

    echo "Contenedor creado: $CONTAINER_NAME con opción $OPTION"
done
