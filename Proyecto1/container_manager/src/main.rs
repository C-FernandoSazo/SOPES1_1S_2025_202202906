use std::{fs, thread, time, process::Command};
use serde::{Deserialize, Serialize};
use reqwest::blocking::Client;
use bollard::Docker;
use bollard::container::{Config, CreateContainerOptions, StartContainerOptions, RemoveContainerOptions, ListContainersOptions};
use signal_hook::consts::SIGINT;
use signal_hook::flag;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::collections::HashMap;
use chrono::{Utc, SecondsFormat};
use prettytable::{Table, row};

// Estructura para deserializar los datos del kernel
#[derive(Debug, Deserialize, Serialize, Clone)]
struct SysInfo {
    Memory: MemoryInfo,
    Docker_Containers: Vec<ContainerInfo>,
}

#[derive(Debug, Deserialize, Serialize, Clone)]
struct MemoryInfo {
    Total_Memory_MB: u64,
    Free_Memory_MB: u64,
    Used_Memory_MB: u64,
    CPU_Usage_Percentage: f64,
}

#[derive(Debug, Deserialize, Serialize, Clone)]
struct ContainerInfo {
    PID: u32,
    Name: String,
    ContainerID: String,
    Cmdline: String,
    MemoryUsage: f64,
    CPUUsage: f64,
    DiskReadMB: u64,
    DiskWriteMB: u64,
    IORead: u64,
    IOWrite: u64,
}

// Logs estructurados con timestamps
#[derive(Debug, Serialize)]
struct LogEntry {
    timestamp: String,
    sysinfo: SysInfo,
}

fn create_log_entry(sysinfo: SysInfo) -> LogEntry {
    LogEntry {
        timestamp: Utc::now().to_rfc3339_opts(SecondsFormat::Millis, true), 
        sysinfo,
    }
}

// Constantes
const SYSINFO_PATH: &str = "/proc/sysinfo_202202906";
const LOGS_CONTAINER: &str = "logs_manager";
const LOGS_ENDPOINT: &str = "http://localhost:5000/logs";

/// **Lee la información del kernel y la deserializa**
fn read_sysinfo() -> Option<SysInfo> {
    let data = fs::read_to_string(SYSINFO_PATH).ok()?;
    serde_json::from_str(&data).ok()
}

/// **Crea el contenedor de logs si no existe**
async fn create_logs_container(docker: &Docker) {
    let containers = docker.list_containers(Some(ListContainersOptions::<String> {
        all: true,
        ..Default::default()
    })).await.unwrap_or_else(|_| vec![]);

    // ✅ Verificar si el contenedor de logs ya existe
    let log_exists = containers.iter().any(|c| {
        c.names.as_ref()
            .map_or(false, |names| names.iter().any(|name| name.contains(LOGS_CONTAINER)))
    });

    if !log_exists {  // ✅ Ahora la variable 'log_exists' está bien definida
        println!("[INFO] Creando contenedor administrador de logs...");
        let config = Config {
            image: Some("python:3.8"),
            cmd: Some(vec!["python", "-m", "http.server", "5000"]),
            ..Default::default()
        };

        docker.create_container(
            Some(CreateContainerOptions {
                name: LOGS_CONTAINER,
                platform: None, 
            }),
            config,
        ).await.ok();

        docker.start_container(LOGS_CONTAINER, None::<StartContainerOptions<String>>).await.ok();
    }
}


/// **Envía logs con timestamp al contenedor de administración**
fn send_log(client: &Client, log_data: &SysInfo) {
    let log_entry = LogEntry {
        timestamp: Utc::now().to_rfc3339(),
        sysinfo: (*log_data).clone(),
    };

    let _ = client.post(LOGS_ENDPOINT).json(&log_entry).send();
}

/// **Clasifica los contenedores por categoría (CPU, RAM, I/O, Disco)**
fn classify_containers(containers: &[ContainerInfo]) -> HashMap<&str, &ContainerInfo> {
    let mut latest_containers = HashMap::new();

    for container in containers {
        let category = if container.Cmdline.contains("--cpu") {
            "cpu"
        } else if container.Cmdline.contains("--vm") {
            "ram"
        } else if container.Cmdline.contains("--io") {
            "io"
        } else if container.Cmdline.contains("--hdd") {
            "disk"
        } else {
            continue;
        };

        latest_containers.insert(category, container);
    }

    latest_containers
}

/// **Gestiona los contenedores eliminando los antiguos**
async fn manage_containers(docker: &Docker, containers: &[ContainerInfo]) {
    let existing_containers = docker.list_containers(Some(ListContainersOptions::<String> {
        all: true,
        ..Default::default()
    })).await.unwrap_or_else(|_| vec![]);

    let latest_containers = classify_containers(containers);

    let required_categories = ["cpu", "ram", "io", "disk"];

    for existing in &existing_containers {
        if let Some(names) = &existing.names {
            let name = names.get(0).map(|s| s.trim_start_matches('/').to_string()).unwrap_or_default();

            // No eliminar el contenedor de logs
            if name == LOGS_CONTAINER {
                continue;
            }

            if let Some(category) = latest_containers.keys().find(|&&k| name.contains(k)) {
                if let Some(latest) = latest_containers.get(category) {
                    if name != latest.ContainerID {
                        println!("[INFO] Eliminando contenedor antiguo: {}", name);
                        let _ = docker.remove_container(&name, Some(RemoveContainerOptions { force: true, ..Default::default() })).await;
                    }
                }
            }
        }
    }
}

/// **Muestra la información del sistema de forma ordenada**
fn print_containers(containers: &[ContainerInfo]) {
    let mut table = Table::new();
    table.add_row(row!["PID", "Nombre", "Container ID", "CPU %", "RAM %", "Disk MB", "IO MB"]);

    for container in containers {
        table.add_row(row![
            container.PID,
            container.Name,
            container.ContainerID,
            format!("{:.2}", container.CPUUsage),
            format!("{:.2}", container.MemoryUsage),
            container.DiskWriteMB,
            container.IOWrite,
        ]);
    }

    table.printstd();
}

/// **Elimina el cronjob al finalizar el servicio**
fn remove_cronjob() {
    println!("[INFO] Eliminando cronjob...");
    let _ = Command::new("crontab").arg("-l")
        .output()
        .map(|output| {
            let new_cron = String::from_utf8_lossy(&output.stdout)
                .lines()
                .filter(|line| !line.contains("docker run"))
                .collect::<Vec<_>>()
                .join("\n");

            let _ = fs::write("/tmp/new_crontab", new_cron);
            let _ = Command::new("crontab").arg("/tmp/new_crontab").output();
            let _ = fs::remove_file("/tmp/new_crontab");
        });
} 

/// **Punto de entrada principal**
#[tokio::main]
async fn main() {
    let stop_flag = Arc::new(AtomicBool::new(false));
    flag::register(SIGINT, stop_flag.clone()).unwrap();

    let client = Client::new();
    let docker = Docker::connect_with_local_defaults().unwrap();

    println!("[INFO] Iniciando servicio de gestión de contenedores...");

    // 1️⃣ Crear el contenedor de logs si no existe
    create_logs_container(&docker).await;

    while !stop_flag.load(Ordering::Relaxed) {
        if let Some(sysinfo) = read_sysinfo() {
            println!("\n[INFO] Información del sistema:");
            println!("Memoria Total: {} MB", sysinfo.Memory.Total_Memory_MB);
            println!("Memoria Libre: {} MB", sysinfo.Memory.Free_Memory_MB);
            println!("CPU Uso: {:.2}%", sysinfo.Memory.CPU_Usage_Percentage);

            println!("\n[INFO] Contenedores:");
            print_containers(&sysinfo.Docker_Containers);

            send_log(&client, &sysinfo);
            manage_containers(&docker, &sysinfo.Docker_Containers).await;
        }

        thread::sleep(time::Duration::from_secs(10));
    }

    println!("\n[INFO] Finalizando servicio...");
    remove_cronjob();

    if let Some(sysinfo) = read_sysinfo() {
        send_log(&client, &sysinfo);
    }

    // 📌 Generar gráficas y mostrarlas en vivo
    generate_graphs(&client);
    show_live_graphs(&client);

    println!("[INFO] Gráficas generadas correctamente. Servicio terminado.");
}

/// **Función para llamar al endpoint de generación de gráficas**
fn generate_graphs(client: &Client) {
    let _ = client.get("http://localhost:5000/generate_graphs").send();
}

/// **Función para iniciar la visualización en tiempo real**
fn show_live_graphs(client: &Client) {
    let _ = client.get("http://localhost:5000/show_graphs").send();
}



