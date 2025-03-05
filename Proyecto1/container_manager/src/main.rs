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

// Updated structure for kernel data
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

// Updated ContainerInfo to match new kernel fields
#[derive(Debug, Deserialize, Serialize, Clone)]
struct ContainerInfo {
    PID: u32,
    Name: String,
    ContainerID: String,
    Cmdline: String,
    MemoryUsage_percent: f64,
    MemoryUsage_MB: u64,
    CPUUsage_percent: f64,
    DiskUse_MB: u64,
    Write_KBytes: u64,
    Read_KBytes: u64,
    IOReadOps: u64,
    IOWriteOps: u64,
}

// Logs structured with timestamps
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

// Constants
const SYSINFO_PATH: &str = "/proc/sysinfo_202202906";
const LOGS_CONTAINER: &str = "logs_manager";
const LOGS_ENDPOINT: &str = "http://localhost:5000/logs";

fn read_sysinfo() -> Option<SysInfo> {
    let data = fs::read_to_string(SYSINFO_PATH).ok()?;
    serde_json::from_str(&data).ok()
}

async fn create_logs_container(docker: &Docker) {
    let containers = docker.list_containers(Some(ListContainersOptions::<String> {
        all: true,
        ..Default::default()
    })).await.unwrap_or_else(|_| vec![]);

    let log_exists = containers.iter().any(|c| {
        c.names.as_ref()
            .map_or(false, |names| names.iter().any(|name| name.contains(LOGS_CONTAINER)))
    });

    if !log_exists {
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

fn send_log(client: &Client, log_data: &SysInfo) {
    let log_entry = LogEntry {
        timestamp: Utc::now().to_rfc3339(),
        sysinfo: (*log_data).clone(),
    };

    let _ = client.post(LOGS_ENDPOINT).json(&log_entry).send();
}

fn classify_containers(containers: &[ContainerInfo]) -> HashMap<&str, Vec<&ContainerInfo>> {
    let mut containers_by_category: HashMap<&str, Vec<&ContainerInfo>> = HashMap::new();

    for container in containers {
        let cmd_lower = container.Cmdline.to_lowercase();

        let category = if cmd_lower.contains("--cpu") {
            "cpu"
        } else if cmd_lower.contains("--vm") {
            "ram"
        } else if cmd_lower.contains("--io") {
            "io"
        } else if cmd_lower.contains("--hdd") {
            "disk"
        } else {
            "unknown"
        };

        containers_by_category
            .entry(category)
            .or_insert_with(Vec::new)
            .push(container);
    }

    containers_by_category
}

async fn manage_containers(docker: &Docker, containers: &[ContainerInfo]) {
    let existing_containers = docker.list_containers(Some(ListContainersOptions::<String> {
        all: true,
        ..Default::default()
    })).await.unwrap_or_else(|_| vec![]);

    let latest_containers = classify_containers(containers);
    let required_categories = ["cpu", "ram", "io", "disk"];
    let mut removed_containers: Vec<(&ContainerInfo, String)> = Vec::new(); // Store (container, category) pairs

    // Step 1: Display containers by category in tables
    println!("\n[INFO] Contenedores por categoría:");
    for category in required_categories.iter().chain(std::iter::once(&"unknown")) {
        if let Some(containers_in_category) = latest_containers.get(category) {
            let mut table = Table::new();
            table.add_row(row!["PID", "Nombre", "Container ID", "CPU %", "RAM %", "RAM MB", "Disk MB", "IO Read Ops", "IO Write Ops"]);
            
            for container in containers_in_category {
                table.add_row(row![
                    container.PID,
                    container.Name,
                    container.ContainerID,
                    format!("{:.2}", container.CPUUsage_percent),
                    format!("{:.2}", container.MemoryUsage_percent),
                    container.MemoryUsage_MB,
                    container.DiskUse_MB,
                    container.IOReadOps,
                    container.IOWriteOps,
                ]);
            }
            
            println!("{}:", category.to_uppercase());
            table.printstd();
            println!(); // Extra newline for readability
        }
    }

    // Step 2: Manage containers and log removals
    for category in required_categories {
        if let Some(containers_in_category) = latest_containers.get(category) {
            let mut sorted_containers = containers_in_category.clone();
            sorted_containers.sort_by(|a, b| b.PID.cmp(&a.PID)); // Sort by PID descending (newest first)

            if sorted_containers.len() > 1 {
                for old_container in sorted_containers.iter().skip(1) {
                    println!("\n[INFO] Eliminando contenedor antiguo: {} (ID: {})", old_container.Name, old_container.ContainerID);
                    match docker.remove_container(&old_container.ContainerID, Some(RemoveContainerOptions {
                        force: true,
                        ..Default::default()
                    })).await {
                        Ok(_) => {
                            println!("\n[DEBUG] Contenedor {} eliminado con éxito", old_container.ContainerID);
                            removed_containers.push((old_container, category.to_string()));
                        }
                        Err(e) => println!("\n[ERROR] Error al eliminar contenedor {}: {:?}", old_container.ContainerID, e),
                    }
                }
            }
        }
    }

    // Step 3: Display table of removed containers
    if !removed_containers.is_empty() {
        println!("\n[INFO] Contenedores eliminados:");
        let mut table = Table::new();
        table.add_row(row!["Container ID", "Categoría", "PID"]); // Add PID as a relevant field

        for (container, category) in &removed_containers {
            table.add_row(row![
                container.ContainerID,
                category,
                container.PID,
            ]);
        }

        table.printstd();
    } else {
        println!("\n[INFO] No se eliminaron contenedores.");
    }
}

// Updated print_containers to reflect new fields
fn print_containers(containers: &[ContainerInfo]) {
    let mut table = Table::new();
    table.add_row(row!["PID", "Nombre", "Container ID", "CPU %", "RAM %", "RAM MB", "Disk MB", "IO Read Ops", "IO Write Ops"]);

    for container in containers {
        table.add_row(row![
            container.PID,
            container.Name,
            container.ContainerID,
            format!("{:.2}", container.CPUUsage_percent),
            format!("{:.2}", container.MemoryUsage_percent),
            container.MemoryUsage_MB,
            container.DiskUse_MB,
            container.IOReadOps,
            container.IOWriteOps,
        ]);
    }

    table.printstd();
}

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

#[tokio::main]
async fn main() {
    let stop_flag = Arc::new(AtomicBool::new(false));
    flag::register(SIGINT, stop_flag.clone()).unwrap();

    let client = Client::new();
    let docker = Docker::connect_with_local_defaults().unwrap();

    println!("[INFO] Iniciando servicio de gestión de contenedores...");

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

    generate_graphs(&client);
    show_live_graphs(&client);

    println!("[INFO] Gráficas generadas correctamente. Servicio terminado.");
}

fn generate_graphs(client: &Client) {
    let _ = client.get("http://localhost:5000/generate_graphs").send();
}

fn show_live_graphs(client: &Client) {
    let _ = client.get("http://localhost:5000/show_graphs").send();
}