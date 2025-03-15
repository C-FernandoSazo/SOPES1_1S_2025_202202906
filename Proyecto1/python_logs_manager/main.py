from fastapi import FastAPI, HTTPException
import json
import os
from pathlib import Path
import matplotlib.pyplot as plt
from pydantic import BaseModel
import time
from dateutil import parser
import numpy as np
import threading

app = FastAPI()

# 📂 Archivo JSON donde se guardarán los logs
LOGS_FILE = Path("/app/logs/logs.json")

# Crear el directorio y el archivo si no existen
def ensure_logs_file():
    LOGS_FILE.parent.mkdir(parents=True, exist_ok=True)
    if not LOGS_FILE.exists():
        with open(LOGS_FILE, "w") as f:
            json.dump([], f)

ensure_logs_file()

# 📌 Modelo de datos para los logs
class LogEntry(BaseModel):
    timestamp: str
    sysinfo: dict

# 📌 Variable global para almacenar datos históricos por ejecución (contenedores)
categories = ["cpu", "ram", "io", "disk"]
data_by_category = {
    cat: {
        "MemoryUsage_percent": [],
        "CPUUsage_percent": [],
        "timestamps": [],
        "latest": None
    } for cat in categories
}

# 📌 Variable global para almacenar datos del sistema por ejecución
system_data = {
    "Free_Memory_MB": [],
    "Used_Memory_MB": [],
    "CPU_Usage_Percentage": [],
    "timestamps": []
}

# 📌 Endpoint para recibir logs desde Rust y almacenarlos en el JSON
@app.post("/logs")
def receive_log(entry: LogEntry):
    try:
        ensure_logs_file()
        with open(LOGS_FILE, "r+") as f:
            logs = json.load(f)
            logs.append(entry.dict())
            f.seek(0)
            json.dump(logs, f, indent=4)

        # Actualizar datos históricos de contenedores en memoria
        containers = entry.sysinfo["Docker_Containers"]
        for container in containers:
            cmd_lower = container["Cmdline"].lower()
            category = (
                "cpu" if "--cpu" in cmd_lower else
                "ram" if "--vm" in cmd_lower else
                "io" if "--io" in cmd_lower else
                "disk" if "--hdd" in cmd_lower else
                "unknown"
            )
            if category in categories:
                data_by_category[category]["MemoryUsage_percent"].append(container["MemoryUsage_percent"])
                data_by_category[category]["CPUUsage_percent"].append(container["CPUUsage_percent"])
                data_by_category[category]["timestamps"].append(parser.isoparse(entry.timestamp))
                data_by_category[category]["latest"] = {
                    "MemoryUsage_MB": container["MemoryUsage_MB"],
                    "DiskUse_MB": container["DiskUse_MB"],
                    "Write_KBytes": container["Write_KBytes"],
                    "Read_KBytes": container["Read_KBytes"],
                    "IOReadOps": container["IOReadOps"],
                    "IOWriteOps": container["IOWriteOps"],
                    "ContainerID": container["ContainerID"]
                }

        # Actualizar datos históricos del sistema en memoria
        system_data["Free_Memory_MB"].append(entry.sysinfo["Memory"]["Free_Memory_MB"])
        system_data["Used_Memory_MB"].append(entry.sysinfo["Memory"]["Used_Memory_MB"])
        system_data["CPU_Usage_Percentage"].append(entry.sysinfo["Memory"]["CPU_Usage_Percentage"])
        system_data["timestamps"].append(parser.isoparse(entry.timestamp))

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

# 📌 Mostrar gráficas en tiempo real
def show_live_graphs():
    while True:
        if not system_data["timestamps"] and not any(data["timestamps"] for data in data_by_category.values()):
            time.sleep(5)
            continue

        # --- Primera gráfica: Datos generales del sistema (dona y líneas) ---
        fig, (ax_donut, ax_line) = plt.subplots(1, 2, figsize=(12, 6))  # Dos subgráficas horizontales
        fig.suptitle("Datos Generales del Sistema", fontsize=16)

        # Gráfica de dona: Memoria Libre y Usada (último dato)
        memory_values = [system_data["Free_Memory_MB"][-1], system_data["Used_Memory_MB"][-1]] if system_data["Free_Memory_MB"] else [0, 0]
        memory_colors = ['#00FF00', '#0000FF']  # Verde para libre, azul para usada
        if sum(memory_values) > 0:
            ax_donut.pie(memory_values, colors=memory_colors, startangle=90, wedgeprops=dict(width=0.3))
            metrics_text = (
                f"Libre: {memory_values[0]} MB\n"
                f"Usada: {memory_values[1]} MB"
            )
            ax_donut.text(0.5, -0.1, metrics_text, ha='center', va='top', fontsize=10, transform=ax_donut.transAxes)
        else:
            ax_donut.text(0.5, 0.5, "Sin datos", ha='center', va='center', fontsize=12)
        ax_donut.set_title("Uso de Memoria")

        # Gráfica de líneas: Uso de CPU (datos actuales)
        if system_data["CPU_Usage_Percentage"]:
            x = np.arange(len(system_data["timestamps"]))
            ax_line.plot(x, system_data["CPU_Usage_Percentage"], color='#FF0000', marker='o', label=f"CPU %: {system_data['CPU_Usage_Percentage'][-1]:.1f}")
            ax_line.set_title("Uso de CPU")
            ax_line.set_ylim(0, 210)  # Rango fijo de 0 a 210
            ax_line.set_xticks(x)
            ax_line.set_xticklabels([ts.strftime('%H:%M:%S') for ts in system_data["timestamps"]], rotation=45, ha='right')
            ax_line.set_ylabel("Porcentaje (%)")
            ax_line.grid(axis='y')
            ax_line.legend(title="Métrica", loc="upper left")
        else:
            ax_line.text(0.5, 0.5, "Sin datos", ha='center', va='center', fontsize=12)
            ax_line.set_title("Uso de CPU")

        plt.tight_layout(rect=[0, 0, 1, 0.95])  # Ajustar para el título superior
        plt.savefig("/app/logs/system_metrics.png")
        plt.close()

        # --- Gráficas de contenedores: Cuatro columnas en un solo archivo ---
        fig, axs = plt.subplots(4, 4, figsize=(22, 16))  # 4 filas, 4 columnas
        fig.suptitle("Uso de Recursos por Categoría de Contenedores", fontsize=16)

        # Configuración para líneas de RAM y CPU
        ram_color = '#FF9999'
        cpu_color = '#66B2FF'

        # Configuración para dona
        donut_colors = ['#FF69B4', '#8A2BE2']

        # Configuración para barras de IO
        io_colors = ['#FFCC99', '#FFD700']

        for i, category in enumerate(categories):
            if data_by_category[category]["latest"] is not None and data_by_category[category]["MemoryUsage_percent"]:
                # --- Gráfica de líneas: RAM % (columna 1) ---
                ax_ram = axs[i, 0]
                x = np.arange(len(data_by_category[category]["timestamps"]))
                ax_ram.plot(x, data_by_category[category]["MemoryUsage_percent"], color=ram_color, marker='o', label=f"RAM %: {data_by_category[category]['MemoryUsage_percent'][-1]:.2f}")
                ax_ram.set_title(f"{category.upper()} - RAM %")
                ax_ram.set_ylim(0, 3)  # Rango fijo de 0 a 210
                ax_ram.set_xticks(x)
                ax_ram.set_xticklabels([ts.strftime('%H:%M:%S') for ts in data_by_category[category]["timestamps"]], rotation=45, ha='right')
                ax_ram.set_ylabel("Porcentaje (%)")
                ax_ram.grid(axis='y')
                ax_ram.legend(title="Métrica", loc="upper left")

                # --- Gráfica de líneas: CPU % (columna 2) ---
                ax_cpu = axs[i, 1]
                ax_cpu.plot(x, data_by_category[category]["CPUUsage_percent"], color=cpu_color, marker='o', label=f"CPU %: {data_by_category[category]['CPUUsage_percent'][-1]:.1f}")
                ax_cpu.set_title(f"{category.upper()} - CPU %")
                ax_cpu.set_ylim(0, 210)  # Rango fijo de 0 a 210
                ax_cpu.set_xticks(x)
                ax_cpu.set_xticklabels([ts.strftime('%H:%M:%S') for ts in data_by_category[category]["timestamps"]], rotation=45, ha='right')
                ax_cpu.set_ylabel("Porcentaje (%)")
                ax_cpu.grid(axis='y')
                ax_cpu.legend(title="Métrica", loc="upper left")

                # --- Gráfica de dona: Escritura y Lectura (columna 3) ---
                ax_donut = axs[i, 2]
                disk_use_kb = data_by_category[category]["latest"]["DiskUse_MB"] * 1024  # Convertir MB a KB
                donut_values = [
                    data_by_category[category]["latest"]["Write_KBytes"],
                    data_by_category[category]["latest"]["Read_KBytes"]
                ]
                total_donut = disk_use_kb if disk_use_kb > 0 else 1  # Evitar división por cero
                donut_percentages = [((v / total_donut) * 100) for v in donut_values]

                if sum(donut_values) > 0:  # Solo graficar si hay datos válidos
                    ax_donut.pie(donut_values, colors=donut_colors, startangle=90, wedgeprops=dict(width=0.3))
                    metrics_text = (
                        f"Escritura: {donut_values[0]} KB ({donut_percentages[0]:.1f}%)\n"
                        f"Lectura: {donut_values[1]} KB ({donut_percentages[1]:.1f}%)"
                    )
                    ax_donut.text(0.5, -0.1, metrics_text, ha='center', va='top', fontsize=10, transform=ax_donut.transAxes)
                else:
                    ax_donut.text(0.5, 0.5, "Sin actividad", ha='center', va='center', fontsize=12)
                ax_donut.set_title(f"{category.upper()} Container - {data_by_category[category]['latest']['ContainerID']} - Disco: {data_by_category[category]['latest']['DiskUse_MB']} MB")

                # --- Gráfica de barras separadas: IO Read Ops vs IO Write Ops (columna 4) ---
                ax_io = axs[i, 3]
                io_values = [
                    data_by_category[category]["latest"]["IOReadOps"],
                    data_by_category[category]["latest"]["IOWriteOps"]
                ]

                io_labels = [
                    f"Lecturas IO: {data_by_category[category]['latest']['IOReadOps']}",
                    f"Escrituras IO: {data_by_category[category]['latest']['IOWriteOps']}"
                ]

                x_io = np.arange(len(io_labels))
                io_bars = ax_io.bar(x_io, io_values, color=io_colors, width=0.25)
                ax_io.set_title("Operaciones IO")
                ax_io.set_xticks(x_io)
                ax_io.set_xticklabels(io_labels, rotation=45, ha='right')
                ax_io.set_ylabel("Operaciones")
                ax_io.grid(axis='y')
                ax_io.legend(io_bars, io_labels, title="Métricas", loc="upper right")
            else:
                # Sin datos
                axs[i, 0].text(0.5, 0.5, "Sin datos", ha='center', va='center', fontsize=12)
                axs[i, 0].set_title(f"{category.upper()} - RAM %")
                axs[i, 1].text(0.5, 0.5, "Sin datos", ha='center', va='center', fontsize=12)
                axs[i, 1].set_title(f"{category.upper()} - CPU %")
                axs[i, 2].text(0.5, 0.5, "Sin datos", ha='center', va='center', fontsize=12)
                axs[i, 2].set_title(f"{category.upper()} Container - N/A - Disco: N/A")
                axs[i, 3].text(0.5, 0.5, "Sin datos", ha='center', va='center', fontsize=12)
                axs[i, 3].set_title("Operaciones IO")

        plt.tight_layout(rect=[0, 0, 1, 0.95])  # Ajustar para el título superior
        plt.savefig("/app/logs/containers_metrics.png")
        plt.close()

        print("[INFO] Gráficas guardadas en /app/logs/")
        time.sleep(5)

# 📌 Iniciar el hilo de gráficas automáticamente al arrancar la aplicación
@app.on_event("startup")
def startup_event():
    global data_by_category, system_data
    # Reiniciar las listas al iniciar el servidor
    data_by_category = {
        cat: {
            "MemoryUsage_percent": [],
            "CPUUsage_percent": [],
            "timestamps": [],
            "latest": None
        } for cat in categories
    }
    system_data = {
        "Free_Memory_MB": [],
        "Used_Memory_MB": [],
        "CPU_Usage_Percentage": [],
        "timestamps": []
    }
    thread = threading.Thread(target=show_live_graphs, daemon=True)
    thread.start()
    print("[INFO] Hilo de gráficas iniciado automáticamente al arrancar la API")

# Ruta raíz
@app.get("/")
def read_root():
    return {"message": "Bienvenido a la API"}