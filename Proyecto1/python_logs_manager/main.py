from fastapi import FastAPI, HTTPException
import json
import os
from datetime import datetime
from pathlib import Path
import matplotlib.pyplot as plt
from pydantic import BaseModel
import time
from dateutil import parser  # Agregar importación



app = FastAPI()

# 📂 Archivo JSON donde se guardarán los logs
LOGS_FILE = Path("/app/logs/logs.json")

# Crear el directorio y el archivo si no existen
def ensure_logs_file():
    LOGS_FILE.parent.mkdir(parents=True, exist_ok=True)  # Crear /app/logs si no existe
    if not LOGS_FILE.exists():
        with open(LOGS_FILE, "w") as f:
            json.dump([], f)

ensure_logs_file()


# 📌 Modelo de datos para los logs
class LogEntry(BaseModel):
    timestamp: str
    sysinfo: dict

# 📌 Endpoint para recibir logs desde Rust y almacenarlos en el JSON
@app.post("/logs")
def receive_log(entry: LogEntry):
    try:
        ensure_logs_file()
        with open(LOGS_FILE, "r+") as f:
            logs = json.load(f)
            logs.append(entry.dict())  # Agrega el nuevo log
            f.seek(0)
            json.dump(logs, f, indent=4)  # Guarda los logs en el archivo
        return {"message": "Log almacenado exitosamente"}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

# 📌 Endpoint para obtener todos los logs almacenados
@app.get("/logs")
def get_logs():
    try:
        with open(LOGS_FILE, "r") as f:
            logs = json.load(f)
        return logs
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

# 📌 **Mostrar gráficas en tiempo real**
def show_live_graphs():
    
    while True:
        with open(LOGS_FILE, "r") as f:
            logs = json.load(f)

        if not logs:
            time.sleep(5)
            continue  # Si no hay logs, esperar y continuar

        timestamps = [parser.isoparse(log["timestamp"]) for log in logs]
        cpu_usage = [log["sysinfo"]["Memory"]["CPU_Usage_Percentage"] for log in logs]
        memory_usage = [log["sysinfo"]["Memory"]["Used_Memory_MB"] for log in logs]

        # 📊 Gráfica de uso de CPU
        plt.figure(figsize=(10, 5))
        plt.plot(timestamps, cpu_usage, marker='o', linestyle='-', color='r')
        plt.xlabel("Tiempo")
        plt.ylabel("Uso de CPU (%)")
        plt.title("Uso de CPU a lo largo del tiempo")
        plt.grid()
        plt.xticks(rotation=45)
        plt.savefig("/app/logs/cpu_usage.png")  # Guardar en volumen compartido

        # 📊 Gráfica de uso de Memoria
        plt.figure(figsize=(10, 5))
        plt.plot(timestamps, memory_usage, marker='o', linestyle='-', color='b')
        plt.xlabel("Tiempo")
        plt.ylabel("Memoria Usada (MB)")
        plt.title("Uso de Memoria a lo largo del tiempo")
        plt.grid()
        plt.xticks(rotation=45)
        plt.savefig("/app/logs/memory_usage.png")  # Guardar en volumen compartido

        print("[INFO] Gráficas guardadas en /app/logs/")
        time.sleep(5)  # Esperar 5 segundos y volver a actualizar


# 📌 **Iniciar la visualización en tiempo real**
@app.get("/show_graphs")
def show_graphs():
    import threading
    thread = threading.Thread(target=show_live_graphs, daemon=True)
    thread.start()
    return {"message": "Gráficas en tiempo real iniciadas"}

# Ruta raíz
@app.get("/")
def read_root():
    return {"message": "Bienvenido a la API"}
