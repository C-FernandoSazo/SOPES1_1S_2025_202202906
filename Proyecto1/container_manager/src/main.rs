use std::{fs, thread, time};
use serde::{Deserialize, Serialize};
use reqwest::blocking::Client;
use bollard::Docker;
use signal_hook::consts::SIGINT;
use signal_hook::flag;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

// Estructura para deserializar los datos del kernel
#[derive(Debug, Deserialize)]
struct SysInfo {
    Memory: MemoryInfo,
    Docker_Containers: Vec<ContainerInfo>,
}

#[derive(Debug, Deserialize)]
struct MemoryInfo {
    Total_Memory_MB: u64,
    Free_Memory_MB: u64,
    Used_Memory_MB: u64,
    CPU_Usage_Percentage: f64,
}

#[derive(Debug, Deserialize)]
struct ContainerInfo {
    PID: u32,
    Name: String,
    Cmdline: String,
    MemoryUsage: f64,
    CPUUsage: f64,
    DiskReadMB: u64,
    DiskWriteMB: u64,
    IORead: String,
    IOWrite: String,
}

const SYSINFO_PATH: &str = "/proc/sysinfo_202202906";
const LOGS_CONTAINER: &str = "logs_manager";
const LOGS_ENDPOINT: &str = "http://localhost:5000/logs";

fn read_sysinfo() -> Option<SysInfo> {
    let data = fs::read_to_string(SYSINFO_PATH).ok()?;
    serde_json::from_str(&data).ok()
}

fn send_log(client: &Client, log_data: &SysInfo) {
    let _ = client.post(LOGS_ENDPOINT).json(&log_data).send();
}

async fn manage_containers(docker: &Docker, containers: &[ContainerInfo]) {
    let existing_containers = docker.list_containers(Some(ListContainersOptions::<String> {
        all: true,
        ..Default::default()
    })).await.unwrap_or_else(|_| vec![]);

    let mut latest_containers = std::collections::HashMap::new();

    for container in containers {
        let category = if container.Cmdline.contains("cpu") {
            "cpu"
        } else if container.Cmdline.contains("ram") {
            "ram"
        } else if container.Cmdline.contains("io") {
            "io"
        } else if container.Cmdline.contains("disk") {
            "disk"
        } else {
            continue;
        };

        latest_containers.insert(category, container.Cmdline.clone());
    }

    for existing in &existing_containers {
        if let Some(names) = &existing.names {
            let name = names.get(0).unwrap_or(&"".to_string()).trim_start_matches('/');
            if name == LOGS_CONTAINER {
                continue;
            }
            if let Some(category) = latest_containers.keys().find(|&&k| name.contains(k)) {
                if let Some(latest) = latest_containers.get(category) {
                    if name != latest {
                        println!("[INFO] Eliminando contenedor antiguo: {}", name);
                        let _ = docker.remove_container(name, Some(RemoveContainerOptions { force: true, ..Default::default() })).await;
                    }
                }
            }
        }
    }
}

fn main() {
    let stop_flag = Arc::new(AtomicBool::new(false));
    flag::register(SIGINT, stop_flag.clone()).unwrap();

    let client = Client::new();
    let docker = Docker::connect_with_local_defaults().unwrap();

    println!("[INFO] Gestor de contenedores iniciado...");

    while !stop_flag.load(Ordering::Relaxed) {
        if let Some(sysinfo) = read_sysinfo() {
            println!("[INFO] Datos del sistema: {:?}", sysinfo);
            send_log(&client, &sysinfo);
            manage_containers(&docker, &sysinfo.Docker_Containers);
        }
        thread::sleep(time::Duration::from_secs(10));
    }

    println!("[INFO] Finalizando servicio y enviando último log...");
    if let Some(sysinfo) = read_sysinfo() {
        send_log(&client, &sysinfo);
    }
}
