#!/bin/bash

# Definir las opciones de consumo
OPTIONS=(
    "--cpu 1 --timeout 300s"       # CPU
    "--io 1 --timeout 300s"        # I/O
    "--vm 2 --vm-bytes 256M --timeout 300s"  # RAM
    "--hdd 1 --hdd-bytes 1G --timeout 300s"  # Disco
)

# Crear 10 contenedores con nombres únicos
for i in {1..1}; do
    # Seleccionar un tipo de contenedor aleatoriamente
    RANDOM_INDEX=$((RANDOM % 4))
    OPTION="${OPTIONS[$RANDOM_INDEX]}"
    
    # Formatear la fecha y la hora en el nombre del contenedor
    CURRENT_DATE=$(date +"%d-%m-%Y_%H%M%S")

    # Generar el nombre del contenedor con tipo de stress y fecha
    CONTAINER_NAME="stress_${CURRENT_DATE}_${i}"

    # Ejecutar el contenedor en segundo plano
    docker run -d --name "$CONTAINER_NAME" containerstack/alpine-stress sh -c "exec stress --vm 2 --vm-bytes 256M"
    docker run -d --name "cpucontainer" containerstack/alpine-stress sh -c "exec stress --cpu 1"
    docker run -d --name "hddcontainer" containerstack/alpine-stress sh -c "exec stress --hdd 1 --hdd-bytes 1G "

    echo "Contenedor creado: $CONTAINER_NAME con opción $OPTION"
done
