#!/bin/bash

# Lista de opciones de estrés para cada tipo de carga
stress_commands=(
    "stress --cpu 2 --timeout 60s"   # Alto consumo de CPU
    "stress --vm 1 --vm-bytes 256M --timeout 60s"  # Alto consumo de RAM
    "stress --io 2 --timeout 60s"    # Alto consumo de I/O
    "stress --hdd 1 --hdd-bytes 500M --timeout 60s"  # Alto consumo de Disco
)

# Nombre de la Imagen
image="containerstack/alpine-stress"

# Crear 10 contenedores aleatorios
for i in {1..10}; do
    # Seleccionar aleatoriamente una de las configuraciones de estrés
    stress_command=${stress_commands[$RANDOM % ${#stress_commands[@]}]}
    
    # Generar un nombre único para el contenedor
    container_name="stress_$(date +%s)_$(head /dev/urandom | tr -dc a-z0-9 | head -c 5)"
    
    echo "Creando contenedor: $container_name con comando: $stress_command"

    # Ejecutar el contenedor con la configuración específica
    docker run -d --name "$container_name" "$image" $stress_command
done
