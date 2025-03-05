#!/bin/bash

# Definir las opciones de consumo
OPTIONS=(
    "--cpu 1"       # CPU
    "--io 1"        # I/O
    "--vm 2 --vm-bytes 256M"  # RAM
    "--hdd 1 --hdd-bytes 1G"  # Disco
)

# Crear 10 contenedores con nombres únicos usando /dev/urandom
for i in {1..8}; do
    # Seleccionar un tipo de contenedor aleatoriamente
    RANDOM_INDEX=$((RANDOM % 4))
    OPTION="${OPTIONS[$RANDOM_INDEX]}"
    
    # Extraer el tipo de contenedor (cpu, io, vm, hdd)
    TYPE=$(echo "$OPTION" | awk '{print $1}' | sed 's/--//')

    # Generar un ID aleatorio de 8 caracteres usando /dev/urandom
    RANDOM_ID=$(cat /dev/urandom | tr -dc 'a-z0-9' | head -c 9)
    
    # Generar el nombre del contenedor con tipo y ID aleatorio
    CONTAINER_NAME="stress_${TYPE}_${RANDOM_ID}"

    # Ejecutar el contenedor en segundo plano
    docker run -d --name "$CONTAINER_NAME" containerstack/alpine-stress sh -c "exec stress $OPTION"

    echo "Contenedor creado: $CONTAINER_NAME con opción $OPTION"
done
