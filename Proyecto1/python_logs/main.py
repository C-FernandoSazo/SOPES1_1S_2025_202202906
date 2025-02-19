from fastapi import FastAPI, HTTPException
from pydantic import BaseModel
import json
import os
import matplotlib.pyplot as plt
from datetime import datetime

# Inicializar FastAPI
app = FastAPI()

# Archivo JSON donde se almacenarán los logs
LOG_FILE = "/logs/logs.json"

# Crear la carpeta /logs si no existe
os.makedirs(os.path.dirname(LOG_FILE), exist_ok=True)

# Clase para la estructura de un log
class LogEntry(BaseModel):
    timestamp: str
    category: str  # cpu, ram, io, disk
    container_id: str
    event: str
    details: dict

# Función para cargar logs
def load_logs():
    if os.path.exists(LOG_FILE):
        with open(LOG_FILE, "r") as file:
            return json.load(file)
    return []

# Función para guardar logs
def save_logs(logs):
    with open(LOG_FILE, "w") as file:
        json.dump(logs, file, indent=4)

# Ruta para agregar un nuevo log
@app.post("/log")
def add_log(log: LogEntry):
    logs = load_logs()
    logs.append(log.dict())
    save_logs(logs)
    return {"message": "Log almacenado correctamente"}

# Ruta para obtener todos los logs
@app.get("/logs")
def get_logs():
    return load_logs()

# Ruta para generar gráfica de logs por categoría
@app.get("/generate_chart")
def generate_chart():
    logs = load_logs()
    
    if not logs:
        raise HTTPException(status_code=404, detail="No hay logs almacenados")
    
    # Contar eventos por categoría
    categories = {"cpu": 0, "ram": 0, "io": 0, "disk": 0}
    for log in logs:
        if log["category"] in categories:
            categories[log["category"]] += 1

    # Generar gráfica
    plt.figure(figsize=(6,4))
    plt.bar(categories.keys(), categories.values(), color=["red", "blue", "green", "purple"])
    plt.xlabel("Categoría")
    plt.ylabel("Cantidad de eventos")
    plt.title("Eventos por Categoría de Contenedor")
    plt.savefig("/logs/log_chart.png")  # Guardar en volumen compartido
    plt.close()
    
    return {"message": "Gráfica generada en /logs/log_chart.png"}
