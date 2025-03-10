# Manual Técnico - Gestor de Contenedores

## Introducción

El **Gestor de Contenedores** es un proyecto que tiene como objetivo la implementación de un sistema capaz de gestionar contenedores de Docker mediante la integración de diferentes tecnologías, como **Bash**, **Rust**, **Python** y **C (módulos del Kernel de Linux)**.

El gestor de contenedores permite:
- La creación, monitoreo y eliminación de contenedores en función del uso de recursos del sistema.
- La captura y almacenamiento de métricas en tiempo real.
- La automatización del proceso de ejecución mediante un cronjob en Bash.
- La administración de logs y generación de gráficas para la visualización de datos.

Este documento describe en detalle la arquitectura del proyecto y la implementación de cada componente.

---

## 1. Script Creador de Contenedores

El **script en Bash** (`script.sh`) tiene la tarea de generar contenedores de manera automática, en base la imagen `https://github.com/containerstack/alpine-stress`.  
Para esto, se hace uso de la herramienta **Docker CLI (Command Line Interface)** que es una herramienta que permite interactuar con el motor de Docker a través de comandos en la terminal, gracias a esta herramienta podemos realizar tareas como:
- Descargar imágenes desde Docker Hub o un repositorio privado.
- Crear y ejecutar contenedores a partir de imágenes.
- Listar y administrar contenedores activos en el sistema.
- Detener, eliminar y reiniciar contenedores según sea necesario  

Y se ejecuta de manera periódica a través de un **cronjob**.  
**Cronjob** es una utilidad de Unix/Linux que permite programar tareas automáticas en intervalos de tiempo específicos. Esto es útil cuando se necesita ejecutar scripts o comandos sin intervención del usuario.

### 1.1 Funcionalidad del Script

1. **Generación de contenedores aleatorios**  
   - Se crean **10 contenedores** en cada ejecución del script.
   - Cada contenedor se basa en la imagen `containerstack/alpine-stress`, que permite simular carga en distintos recursos del sistema.

2. **Tipos de Contenedores Generados**
   - **CPU:** Genera carga en el procesador.
   - **RAM:** Consume una cantidad específica de memoria.
   - **I/O:** Simula operaciones intensivas de entrada/salida.
   - **Disco:** Simula uso intensivo de escritura en disco.

3. **Nombres de los Contenedores**
   - Se generan dinámicamente utilizando el módulo especial de Unix `/dev/urandom` para garantizar unicidad.
   - Se establece un prefijo según el tipo de consumo (`stress_cpu`, `stress_ram`, etc.).

---

### 1.2 Código del Script en Bash

```bash
#!/bin/bash

# Opciones de consumo
OPTIONS=(
    "--cpu 1"       # CPU
    "--io 1"        # I/O
    "--vm 2 --vm-bytes 256M"  # RAM
    "--hdd 1 --hdd-bytes 1G"  # Disco
)

# Se crean 10 contenedores con nombres únicos usando /dev/urandom
for i in {1..10}; do
    # Seleccionar un tipo de contenedor aleatoriamente
    RANDOM_INDEX=$((RANDOM % 4))
    OPTION="${OPTIONS[$RANDOM_INDEX]}"
    
    # Extraer el tipo de contenedor
    TYPE=$(echo "$OPTION" | awk '{print $1}' | sed 's/--//')

    # Generar un ID aleatorio de 8 caracteres
    RANDOM_ID=$(cat /dev/urandom | tr -dc 'a-z0-9' | head -c 8)
    
    # Generar el nombre del contenedor
    CONTAINER_NAME="stress_${TYPE}_${RANDOM_ID}"

    # Ejecutar el contenedor en segundo plano
    docker run -d --name "$CONTAINER_NAME" containerstack/alpine-stress sh -c "exec stress $OPTION"

    echo "Contenedor creado: $CONTAINER_NAME con opción $OPTION (Tipo: $TYPE)"
done
```

---

## 2. Módulo Kernel

El módulo kernel `sysinfo_202202906` es una extensión del sistema operativo diseñada para monitorear y proporcionar información detallada sobre el uso de recursos (memoria, CPU y E/S de disco) de contenedores Docker que ejecutan procesos con el comando `stress`. Este módulo se integra con el sistema de archivos `/proc` mediante la creación de un archivo especial `/proc/sysinfo_202202906`, que muestra los datos obtenidos en formato JSON. Su propósito principal es ofrecer una visión en tiempo real de las métricas de rendimiento de los contenedores, enfocándose en los procesos padres (contenedores principales) para evitar redundancias.  

Este módulo **interactúa directamente con el Kernel de Linux** y permite **monitorear y exponer información crítica** del sistema en un formato estructurado, facilitando su uso por otros componentes del sistema. La información generada por el módulo es utilizada por el **servicio en Rust**, que se encarga de la gestión de contenedores basada en estas métricas.

### Funcionalidad Principal
El módulo recopila y presenta las siguientes métricas por contenedor Docker:
- **PID**: Identificador del proceso principal.
- **Nombre**: Nombre del proceso (en este caso, "stress").
- **ContainerID**: Identificador único del contenedor Docker.
- **Cmdline**: Línea de comando ejecutada por el proceso.
- **MemoryUsage_percent**: Porcentaje de uso de memoria en relación con la memoria total del sistema.
- **MemoryUsage_MB**: Uso de memoria en megabytes (MB).
- **CPUUsage_percent**: Porcentaje de uso de CPU.
- **DiskUse_MB**: Uso total de disco (lectura + escritura) en MB.
- **Write_KBytes**: Bytes escritos en kilobytes (KB).
- **Read_KBytes**: Bytes leídos en kilobytes (KB).
- **IOReadOps**: Número de operaciones de lectura de E/S.
- **IOWriteOps**: Número de operaciones de escritura de E/S.

Además, incluye una sección global de memoria que muestra:
- Total, libre y usado en MB.
- Uso de CPU del sistema en porcentaje.

## Secciones del Módulo

1. **Inclusión de Headers y Definiciones**
   - Incluye bibliotecas como `<linux/module.h>`, `<linux/proc_fs.h>`, `<linux/cgroup.h>`, entre otras, necesarias para interactuar con el kernel, el sistema de archivos `/proc`, y los cgroups.
   - Define constantes como `PROC_NAME` (nombre del archivo `/proc`), `MAX_CMDLINE_LENGTH` (tamaño máximo de la línea de comandos).

2. **Funciones de Utilidad**
   - **`get_process_cmdline`**: Extrae la línea de comandos de un proceso desde su memoria virtual, reemplazando caracteres nulos por espacios para una representación legible.
   - **`get_container_id`**: Obtiene el ID del contenedor a partir de la ruta del cgroup asociada al proceso, buscando el prefijo "docker-" y delimitando hasta ".scope".
   - **`get_cpu_usage`**: Calcula el uso total de CPU del sistema en general, basado en estadísticas de `kcpustat_cpu`.
   - **`is_parent_process`**: Verifica si un proceso es padre examinando si tiene hijos en su lista `children`.
   - **`get_memory_usage`**: Lee el archivo `memory.current` del cgroup para obtener el uso de memoria en bytes, convertido a MB.
   - **`get_cpu_usage_container`**: Extrae el valor `usage_usec` del archivo `cpu.stat` para medir el uso de CPU del contenedor.
   - **`get_io_stats`**: Parsea el archivo `io.stat` para obtener bytes leídos (`rbytes`), bytes escritos (`wbytes`), y operaciones de I/O (`rios`, `wios`), devolviendo estos valores en KB y como conteos de operaciones.

3. **Función Principal de Salida (`sysinfo_show`)**
   - Itera sobre todos los procesos usando `for_each_process`, filtrando por `stress` y padres.
   - Desde aca se llaman las distintas funciones para recolectar las métricas como `MemoryUsage_percent`, `CPUUsage_percent`, y estadísticas de disco (`DiskUse_MB`, `Write_KBytes`, `Read_KBytes`).
   - Genera la salida JSON con las métricas recolectadas.

4. **Gestión del Archivo `/proc`**
   - **`sysinfo_open`**: Inicializa la lectura del archivo `/proc/sysinfo_202202906` usando `single_open`.
   - **`sysinfo_ops`**: Define las operaciones del archivo (`proc_open` y `proc_read`).

5. **Inicialización y Cierre**
   - **`sysinfo_init`**: Registra el módulo y crea el archivo `/proc`.
   - **`sysinfo_exit`**: Elimina el archivo `/proc` al descargar el módulo.

---

## Uso de cgroups para Obtener Métricas de Docker Containers

Los **cgroups** (control groups) son una característica del kernel de Linux que permiten la organización, control y monitoreo del uso de recursos (CPU, memoria, E/S, etc.) de procesos o grupos de procesos, como los contenedores Docker. En este módulo, se utilizan los cgroups para identificar y extraer métricas específicas de los contenedores Docker mediante sus IDs. Aquí está el proceso y las rutas utilizadas:

### Estructura de cgroups
Los cgroups están organizados en una jerarquía de subsistemas, cada uno asociado a un tipo de recurso. Esta jerarquía se representa como un árbol de directorios en el sistema de archivos virtual `/sys/fs/cgroup/`. Cada subsistema gestiona un recurso específico, y los procesos se asignan a estos grupos mediante identificadores.

### Subsistemas
Los subsistemas son los controladores que gestionan diferentes tipos de recursos. Algunos ejemplos relevantes incluyen:
- **`cpu`**: Controla el uso de CPU, permitiendo límites y prioridades mediante parámetros como `cpu.cfs_quota_us` y `cpu.cfs_period_us`.
- **`memory`**: Gestiona el uso de memoria, con archivos como `memory.current` y `memory.limit_in_bytes`.
- **`io`**: Monitorea y limita las operaciones de entrada/salida, con estadísticas en `io.stat`.
- **`cpuset`**: Asigna conjuntos específicos de CPUs y nodos de memoria a un grupo.
- **`blkio`**: Controla el ancho de banda de I/O de bloques (discos).

### Proceso General
1. **Identificación del Contenedor**
   - La función `get_container_id` utiliza la API de cgroups (`task_cgroup` y `cgroup_path`) para obtener la ruta del cgroup asociada a un `task_struct`.  
   Busca el patrón "docker-" en la ruta (e.j., `/system.slice/docker-<container_id>.scope`) y extrae el ID del contenedor hasta el sufijo ".scope". Esto se logra con `strstr` para localizar "docker-" y delimitar con ".scope".

2. **Acceso a Métricas**
   - Una vez identificado el ID, el módulo accede a archivos específicos dentro del sistema de archivos cgroup para obtener las métricas:
     - **Memoria**: Usa `memory.current` para el uso de memoria en bytes.
     - **CPU**: Usa `cpu.stat` para el uso acumulado de CPU en microsegundos (`usage_usec`).
     - **E/S**: Usa `io.stat` para bytes leídos (`rbytes`) y escritos (`wbytes`), así como operaciones de E/S (`rios` y `wios`).

3. **Conversión y Cálculo**
   - Las métricas raw se convierten a unidades más útiles (MB, KB, porcentaje) y se calculan diferencias temporales para tasas (e.g., uso de CPU porcentual).

### Rutas Utilizadas
Las rutas en el sistema de archivos cgroup se construyen dinámicamente usando el ID del contenedor. Los prefijos y archivos específicos son:
- **Memoria**: `/sys/fs/cgroup/system.slice/docker-<container_id>.scope/memory.current`
  - Contiene el uso de memoria actual en bytes.
- **CPU**: `/sys/fs/cgroup/system.slice/docker-<container_id>.scope/cpu.stat`
  - Contiene estadísticas de CPU, incluyendo `usage_usec` (microsegundos de uso).
- **E/S**: `/sys/fs/cgroup/system.slice/docker-<container_id>.scope/io.stat`
  - Contiene estadísticas de E/S, incluyendo `rbytes` (bytes leídos), `wbytes` (bytes escritos), `rios` (operaciones de lectura), y `wios` (operaciones de escritura).

## Compilación e Instalación del Módulo

1. **Compilar el módulo**
   ```sh
   make
   ```

2. **Cargar el módulo en el Kernel**
   ```sh
   sudo insmod sysinfo_202202906.ko
   ```

3. **Verificar que el módulo está cargado**
   ```sh
   lsmod | grep sysinfo
   ```

4. **Leer la información desde `/proc`**
   ```sh
   cat /proc/sysinfo_202202906
   ```

5. **Eliminar el módulo**
   ```sh
   sudo rmmod sysinfo_202202906
   ```

### Ejemplo de ejecución módulo

```bash
# Leer la información desde el modulo
cat /proc/sysinfo_202202906

# Salida de los datos
{
  "Memory": {
    "Total_Memory_MB": 31874,
    "Free_Memory_MB": 17817,
    "Used_Memory_MB": 14057,
    "CPU_Usage_Percentage": 16.43
  },
  "Docker_Containers": [
    {
      "PID": 52643,
      "Name": "stress",
      "ContainerID": "c87b60cf51b9",
      "Cmdline": "stress --io 1 ",
      "MemoryUsage_percent": 0.00,
      "MemoryUsage_MB": 0,
      "CPUUsage_percent": 0.00,
      "DiskUse_MB": 0,
      "Write_KBytes": 0,
      "Read_KBytes": 0,
      "IOReadOps": 0,
      "IOWriteOps": 20830
    }
  ]
}
```

---

## 3. Servicio de Rust

El servicio desarrollado en **Rust** es el núcleo del proyecto, encargado de coordinar la comunicación y las funcionalidades entre diferentes componentes, incluyendo la interacción con el módulo kernel `sysinfo_202202906`, la gestión de contenedores Docker, y un servicio de logs implementado en Python.  

El **servicio en Rust** es una aplicación de backend que actúa como un orquestador para el monitoreo y gestión de contenedores Docker en el contexto del proyecto. Utiliza bibliotecas como `bollard` para interactuar con la API de Docker, `serde` para serialización/deserialización de datos JSON, `reqwest` para peticiones HTTP, y `signal_hook` para manejar señales de terminación. Este servicio se ejecuta en un bucle infinito, realizando tareas periódicas cada 10 segundos hasta que se interrumpe con una señal como `Ctrl + C`.

## Propósito del Servicio
El propósito principal del servicio es:
- **Centralizar la Comunicación**: Actúa como el punto de integración entre el módulo kernel, los contenedores Docker y el servicio de logs, facilitando el flujo de datos y comandos.
- **Gestión de Contenedores**: Crear, monitorear y eliminar contenedores, incluyendo un contenedor dedicado para logs, asegurando un manejo eficiente de recursos.
- **Análisis y Registro**: Analizar las métricas recolectadas del módulo kernel, generar logs estructurados y enviarlos a un contenedor de logs para su almacenamiento y visualización.
- **Automatización**: Ejecutar tareas programadas (e.g., generación de gráficas) y gestionar cronjobs al inicio y fin del servicio.

## Rol en la Gestión de Ejecución y Eliminación de Contenedores
El servicio en Rust es el encargado de gestionar el ciclo de vida de los contenedores Docker creados por el `script.sh`, cumpliendo con los siguientes puntos específicos:

1. **Creación del Contenedor de Logs al Iniciar**:
   - Al arrancar, el servicio crea un contenedor llamado `logs_managerr` basado en la imagen `python:3.9`, ejecutando un servidor HTTP simple en el puerto 5000. Este contenedor recibe y almacena las peticiones HTTP con los logs generados.
   - Verifica si el contenedor ya existe para evitar duplicados, utilizando `list_containers` de la API de Docker.

2. **Ejecución en Bucle Infinito**:
   - Opera en un bucle que se ejecuta cada 10 segundos, manejado por `thread::sleep(time::Duration::from_secs(10))`, hasta recibir la señal `SIGINT`.
   - Durante cada iteración, realiza las siguientes tareas:
     - **a. Lectura del Archivo `/proc/sysinfo_202202906`**: Lee los datos generados por el módulo kernel usando `fs::read_to_string`.
     - **b. Deserialización del Contenido**: Convierte el JSON recibido en una estructura `SysInfo` usando `serde_json::from_str`.
     - **c. Análisis para Gestión de Contenedores**: Clasifica los contenedores por categoría (CPU, RAM, I/O, disco) con `classify_containers` y gestiona su eliminación si hay más de un contenedor por categoría, priorizando los más recientes.
     - **d. Generación de Logs**: Crea entradas de log con marcas de tiempo usando `create_log_entry` y las envía al contenedor de logs con `send_log`.
     - **e. Peticiones HTTP**: Envía los logs al endpoint `http://localhost:5000/logs`.

3. **Eliminación al Finalizar**:
   - Al recibir `SIGINT`, envía una última petición de logs, genera gráficas con `generate_graphs`, muestra gráficos en vivo con `show_live_graphs`, y elimina un cronjob asociado con `remove_cronjob`.

## Interacción con el Módulo del Kernel
El servicio interactúa directamente con el módulo kernel `sysinfo_202202906` para obtener las métricas de los contenedores Docker:
- **Lectura de Datos**: Utiliza la función `read_sysinfo` para leer el archivo `/proc/sysinfo_202202906`, que contiene información en formato JSON generada por el módulo kernel. Este archivo incluye datos como `MemoryUsage_percent`, `CPUUsage_percent`, `DiskUse_MB`, y operaciones de I/O.
- **Estructura de Datos**: La información se deserializa en una estructura `SysInfo`, que refleja las métricas definidas en el módulo (e.g., `MemoryInfo` y `ContainerInfo`), asegurando compatibilidad entre el kernel y el servicio.
- **Dependencia**: El servicio depende de que el módulo kernel esté cargado y actualizado, ya que las métricas se generan dinámicamente al leer el archivo `/proc`.

## Interacción con el Servicio de Logs en Python
El servicio de logs, ejecutado en el contenedor `logs_managerr` (basado en `python:3.9` con un servidor HTTP), es responsable de recibir, almacenar y procesar los logs enviados por el servicio Rust:
- **Recepción de Logs**: El servicio Rust envía peticiones HTTP POST a `http://localhost:5000/logs` con datos serializados como `LogEntry` (conteniendo marca de tiempo y `SysInfo`).
- **Generación de Gráficas**: Al finalizar, el servicio Rust realiza una petición GET a `http://localhost:5000/generate_graphs` para que el servicio de logs genere visualizaciones basadas en los datos recolectados.
- **Visualización en Vivo**: Otra petición GET a `http://localhost:5000/show_graphs` permite mostrar gráficos en tiempo real, asumiendo que el servicio Python implementa esta funcionalidad.
- **Integración**: El contenedor de logs actúa como un servidor pasivo que procesa las solicitudes del servicio Rust, destacando la separación de responsabilidades entre la gestión y el análisis/visualización.

## Compilación e Instalación

1. **Compilar el proyecto**
   ```sh
   # Con esto se genera el ejecutable
   cargo build --release
   ```

2. **Ejecución del Servicio**
   ```sh
   # Modo debug
   sudo cargo run
   # Modo release
   sudo ./target/release/nombre_del_proyecto
   ```

### Ejemplo de ejecución
```bash
sudo target/release/container_manager

[INFO] Iniciando servicio de gestión de contenedores...
[INFO] Creando contenedor administrador de logs...
[INFO] Contenedor creado exitosamente.
[INFO] Contenedor iniciado correctamente.

[INFO] Información del sistema:
Memoria Total: 31874 MB
Memoria Libre: 16980 MB
CPU Uso: 17.16%

[INFO] Contenedores por categoría:
RAM:
+-------+--------+--------------+--------+-------+--------+---------+-------------+--------------+
| PID   | Nombre | Container ID | CPU %  | RAM % | RAM MB | Disk MB | IO Read Ops | IO Write Ops |
+-------+--------+--------------+--------+-------+--------+---------+-------------+--------------+
| 52792 | stress | 639646af397e | 199.09 | 0.45  | 144    | 0       | 0           | 0            |
+-------+--------+--------------+--------+-------+--------+---------+-------------+--------------+
| 53106 | stress | 57a511dff574 | 199.08 | 0.72  | 231    | 0       | 0           | 0            |
+-------+--------+--------------+--------+-------+--------+---------+-------------+--------------+
| 53182 | stress | b66ad8295cbe | 199.09 | 1.59  | 508    | 0       | 0           | 0            |
+-------+--------+--------------+--------+-------+--------+---------+-------------+--------------+

IO:
+-------+--------+--------------+-------+-------+--------+---------+-------------+--------------+
| PID   | Nombre | Container ID | CPU % | RAM % | RAM MB | Disk MB | IO Read Ops | IO Write Ops |
+-------+--------+--------------+-------+-------+--------+---------+-------------+--------------+
| 52643 | stress | c87b60cf51b9 | 0.00  | 0.00  | 0      | 0       | 0           | 44946        |
+-------+--------+--------------+-------+-------+--------+---------+-------------+--------------+
| 52719 | stress | 859b7c1f0834 | 0.00  | 0.00  | 0      | 0       | 0           | 44906        |
+-------+--------+--------------+-------+-------+--------+---------+-------------+--------------+
| 52879 | stress | b85dcbe34fc4 | 0.00  | 0.00  | 0      | 0       | 0           | 44498        |
+-------+--------+--------------+-------+-------+--------+---------+-------------+--------------+

DISK:
+-------+--------+--------------+-------+-------+--------+---------+-------------+--------------+
| PID   | Nombre | Container ID | CPU % | RAM % | RAM MB | Disk MB | IO Read Ops | IO Write Ops |
+-------+--------+--------------+-------+-------+--------+---------+-------------+--------------+
| 52954 | stress | 03a6b1f39583 | 0.00  | 3.32  | 1061   | 3636519 | 0           | 30022926     |
+-------+--------+--------------+-------+-------+--------+---------+-------------+--------------+
| 53026 | stress | ef3d92034e5d | 18.07 | 1.23  | 395    | 3639868 | 0           | 30054643     |
+-------+--------+--------------+-------+-------+--------+---------+-------------+--------------+

[INFO] Eliminando contenedor antiguo: stress (ID: 57a511dff574)
[DEBUG] Contenedor 57a511dff574 eliminado con éxito

[INFO] Eliminando contenedor antiguo: stress (ID: 639646af397e)
[DEBUG] Contenedor 639646af397e eliminado con éxito

[INFO] Eliminando contenedor antiguo: stress (ID: 859b7c1f0834)
[DEBUG] Contenedor 859b7c1f0834 eliminado con éxito

[INFO] Eliminando contenedor antiguo: stress (ID: c87b60cf51b9)
[DEBUG] Contenedor c87b60cf51b9 eliminado con éxito

[INFO] Eliminando contenedor antiguo: stress (ID: 03a6b1f39583)
[DEBUG] Contenedor 03a6b1f39583 eliminado con éxito

[INFO] Contenedores eliminados:
+--------------+-----------+-------+
| Container ID | Categoría | PID   |
+--------------+-----------+-------+
| 57a511dff574 | ram       | 53106 |
+--------------+-----------+-------+
| 639646af397e | ram       | 52792 |
+--------------+-----------+-------+
| 859b7c1f0834 | io        | 52719 |
+--------------+-----------+-------+
| c87b60cf51b9 | io        | 52643 |
+--------------+-----------+-------+
| 03a6b1f39583 | disk      | 52954 |
+--------------+-----------+-------+
^C
[INFO] Finalizando servicio...
[INFO] Eliminando cronjob...
[INFO] Gráficas generadas correctamente. Servicio terminado.
```

---

## 4. Contenedor administrador de logs

 Este administrador esta diseñado para actuar como un servidor en Python que recibe, almacena y procesa los logs generados por el servicio en Rust. Este contenedor, basado en la imagen `python:3.9`, utiliza tecnologías como FastAPI y Matplotlib para proporcionar una solución robusta y visualmente interactiva.

 ## Propósito del Contenedor de Logs
El propósito principal del contenedor administrador de logs es:
- **Recibir y Almacenar Logs**: Actuar como un servidor HTTP que recibe peticiones del servicio Rust, que son para mandar constantemente los datos recopilados, almacenando estos registros en un archivo JSON persistente para su posterior análisis.
- **Generar Gráficas**: Proporcionar una funcionalidad para crear y mostrar visualizaciones basadas en los logs almacenados, activada por peticiones del servicio Rust al finalizar su ejecución.
- **Accesibilidad**: Hacer que los logs y las gráficas sean accesibles tanto desde la máquina host como desde el contenedor mediante volúmenes compartidos, facilitando la inspección y el uso de los datos.

## Interacción con el Servicio en Rust
El contenedor de logs interactúa estrechamente con el servicio en Rust, que actúa como el orquestador del proyecto:
- **Envío de Logs**: El servicio Rust envía peticiones HTTP POST a la ruta `/logs` del contenedor cada 10 segundos, enviando datos serializados como `LogEntry` (con marca de tiempo y `SysInfo`) generados a partir de las métricas del módulo kernel.
- **Finalización del Servicio**: Al recibir una señal de terminación (e.g., `Ctrl + C`), el servicio Rust realiza una última petición POST a `/logs`, seguida de una petición GET a `/generate_graphs` para crear las gráficas y otra a `/show_graphs` para iniciar la visualización en tiempo real.
- **Dependencia**: El contenedor depende de que el servicio Rust proporcione datos consistentes y válidos, mientras que el servicio Rust confía en que el contenedor procese y almacene los logs correctamente.

## Tecnologías Utilizadas
- **Docker**: El contenedor se ejecuta dentro de un entorno aislado creado por Docker, utilizando la imagen `python:3.9` como base. Esto permite una ejecución portable y reproducible, con el comando `python -m http.server 5000` inicializando un servidor básico que FastAPI reemplaza.
- **FastAPI**: Un framework asíncrono para construir APIs en Python, utilizado para definir endpoints como `/logs`, `/show_graphs` y `/generate_graphs`. Proporciona un manejo eficiente de solicitudes HTTP y validación de datos con Pydantic.
- **Matplotlib**: Biblioteca de visualización de datos en Python, empleada para generar gráficas de uso de CPU y memoria a partir de los logs almacenados, guardando los resultados como imágenes PNG.
- **Pydantic**: Utilizado para definir el modelo `LogEntry`, asegurando la validación y estructura de los datos recibidos de Rust.
- **Volúmenes Compartidos**: Permiten la persistencia de los logs y gráficas, montando el directorio `/app/logs` en el host para acceso externo.

## Arquitectura del Contenedor
- **Estructura**: El contenedor se basa en una imagen `python:3.9` personalizada con el archivo `main.py`. Ejecuta un servidor FastAPI que escucha en el puerto 5000, procesando solicitudes entrantes y gestionando archivos en el directorio `/app/logs`.
- **Flujo de Datos**:
  1. El servicio Rust envía datos JSON a `/logs`.
  2. FastAPI los almacena en `logs.json` dentro de `/app/logs`.
  3. Al recibir `/generate_graphs`, Matplotlib genera imágenes basadas en los datos.
  4. `/show_graphs` inicia un hilo para actualizar las gráficas en tiempo real.
- **Volúmenes Compartidos**: El directorio `/app/logs` se monta como un volumen en el host (e.g., `./logs:/app/logs`), asegurando que los archivos `logs.json`, `cpu_usage.png` y `memory_usage.png` sean accesibles fuera del contenedor.

## Endpoints Principales
- **`/logs` (POST)**: Recibe un `LogEntry` de Rust, lo agrega al archivo `logs.json` y devuelve un mensaje de confirmación. Ejemplo de uso: `curl -X POST "http://localhost:5000/logs" -H "Content-Type: application/json" -d '{"timestamp": "2023-01-01T00:00:00Z", "sysinfo": {...}}'`.
- **`/logs` (GET)**: Devuelve la lista completa de logs almacenados en `logs.json` como JSON.
- **`/show_graphs` (GET)**: Inicia un hilo para mostrar gráficas en tiempo real, actualizando `cpu_usage.png` y `memory_usage.png` cada 5 segundos.
- **`/generate_graphs` (GET)**: Genera y guarda las gráficas basadas en todos los logs almacenados, sin mostrarlas en tiempo real.

## Almacenamiento de Logs en un Archivo JSON Persistente
- **Mecanismo**: El archivo `logs.json` se crea en `/app/logs` con `ensure_logs_file()` si no existe. Cada petición POST a `/logs` lee el archivo, agrega el nuevo `LogEntry` y lo guarda con indentación para legibilidad.
- **Persistencia**: El uso de volúmenes compartidos asegura que los datos persistan más allá de la vida del contenedor, accesibles desde el host en el directorio montado (e.g., `./logs/logs.json`).
- **Estructura**: El archivo contiene un arreglo de objetos `LogEntry`, cada uno con un campo `timestamp` (formato RFC3339) y `sysinfo` (diccionario con métricas del módulo kernel).

## Generación de Gráficas con Matplotlib

- **Proceso**:
  1. La función `show_live_graphs` gestiona la generación de dos tipos de gráficas en tiempo real:
     - **Gráfica del Sistema (`system_metrics.png`)**:
       - Lee los datos actuales del sistema almacenados en la variable global `system_data`, que se actualiza con cada nuevo log recibido en el endpoint `/logs`.
       - Extrae `timestamps`, `Free_Memory_MB`, `Used_Memory_MB` y `CPU_Usage_Percentage`.
       - Crea dos subgráficas:
         - **Uso de Memoria**: Una gráfica de dona con el último valor de `Free_Memory_MB` (verde) y `Used_Memory_MB` (azul), mostrando los valores en MB debajo (ej. "Libre: 2048 MB", "Usada: 1024 MB").
         - **Uso de CPU**: Una línea roja con marcadores, mostrando el porcentaje de CPU a lo largo del tiempo para la ejecución actual del servidor.
       - Guarda la gráfica como `system_metrics.png` en `/app/logs`.
     - **Gráfica de Contenedores (`containers_metrics.png`)**:
       - Lee los datos actuales de los contenedores almacenados en la variable global `data_by_category`, que también se actualiza con cada nuevo log recibido.
       - Para cada categoría (`cpu`, `ram`, `io`, `disk`), genera cuatro columnas:
         - **RAM %**: Línea rosa claro con marcadores, mostrando el porcentaje de uso de memoria (`MemoryUsage_percent`) con 2 decimales.
         - **CPU %**: Línea azul claro con marcadores, mostrando el porcentaje de uso de CPU (`CPUUsage_percent`) con 1 decimal.
         - **Disco**: Dona con `Write_KBytes` (rosa fuerte) y `Read_KBytes` (morado), mostrando valores crudos y porcentajes debajo (ej. "Escritura: 200 KB (40.0%)", "Lectura: 300 KB (60.0%)").
         - **Operaciones IO**: Barras separadas para `IOReadOps` (naranja claro) y `IOWriteOps` (amarillo), con valores en la leyenda.
       - Guarda la gráfica como `containers_metrics.png` en `/app/logs`.
  2. Ambas gráficas se actualizan cada 5 segundos en modo en vivo mientras el servidor está activo.

- **Activación**:
  - **Automática**: Al iniciar el servidor, el evento `@app.on_event("startup")` lanza un hilo continuo con `show_live_graphs`, reiniciando las variables globales `system_data` y `data_by_category` para comenzar con datos frescos en cada ejecución.
  - **Manual**:
    - La petición `GET /generate_graphs` (a implementar) ejecutará el proceso de generación de gráficas manualmente una vez, útil para el final del servicio Rust.
    - La petición `GET /show_graphs` (a implementar) iniciará o reiniciará el hilo continuo de `show_live_graphs`, permitiendo control externo sobre la actualización en vivo.

- **Visualización**:
  - Las imágenes generadas (`system_metrics.png` y `containers_metrics.png`) se almacenan en el directorio `/app/logs`, accesible desde un volumen compartido.
  - Esto permite su uso externo, como visualización en un navegador, integración en dashboards, o análisis adicional.

## Ejecución del Contenedor

1. **Construccion del Contenedor**
   ```sh
   docker build -t python_logs_manager .
   ```

2. **Ejecución del Contenedor**
   ```sh
   docker run -d --name python_logs_manager -p 5000:5000 -v $(pwd)/logs:/app/logs python_logs_manager
   ```

---


## **Conclusión**  

El **Gestor de Contenedores** desarrollado en este proyecto es un sistema integral que combina diversas tecnologías para lograr la creación, monitoreo y administración eficiente de contenedores Docker. A través de la implementación de **Bash, Rust, Python y un módulo del Kernel en C**, se consigue un flujo automatizado y estructurado que permite:  

**Automatización de Contenedores**: Un **script en Bash** gestiona la creación y ejecución de contenedores Docker, garantizando una carga de trabajo equilibrada para distintos recursos del sistema.  

**Monitoreo en Bajo Nivel**: Un **módulo del Kernel en C** interactúa directamente con Linux para obtener métricas en tiempo real sobre memoria, CPU y operaciones de I/O de los contenedores activos.  

**Orquestación Inteligente**: Un **servicio en Rust** actúa como el núcleo del proyecto, procesando las métricas del Kernel, decidiendo la eliminación de contenedores según su uso y asegurando que la ejecución del sistema sea eficiente.  

**Gestión y Visualización de Datos**: Un **contenedor administrador de logs en Python**, basado en FastAPI, almacena y procesa los datos recibidos, permitiendo la visualización en tiempo real de métricas mediante gráficas generadas con `matplotlib`.  

Gracias a esta arquitectura modular y escalable, el proyecto proporciona un sistema eficiente para la administración de contenedores en entornos de alto rendimiento. La combinación de tecnologías de **bajo y alto nivel** permite una integración fluida entre la supervisión de hardware, la automatización de despliegue y el análisis de datos, logrando un equilibrio entre rendimiento y usabilidad.