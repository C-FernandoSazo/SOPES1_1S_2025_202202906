#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h> 
#include <linux/seq_file.h> 
#include <linux/file.h>  
#include <linux/mm.h> 
#include <linux/sched.h> 
#include <linux/uaccess.h>
#include <linux/tty.h>
#include <linux/sched/signal.h>
#include <linux/fs.h>        
#include <linux/slab.h>      
#include <linux/sched/mm.h>
#include <linux/binfmts.h>
#include <linux/timekeeping.h>
#include <linux/proc_fs.h>
#include <linux/pid.h>
#include <linux/cgroup-defs.h>
#include <linux/cgroup.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/mutex.h>
#include <linux/list.h>

static LIST_HEAD(cpu_usage_list); // Global list to track usage data
static DEFINE_MUTEX(cpu_usage_lock); // Mutex for thread safety

MODULE_LICENSE("GPL");
MODULE_AUTHOR("202202906");
MODULE_DESCRIPTION("Módulo para monitorear memoria, CPU y procesos Docker");
MODULE_VERSION("1.0");

#define PROC_NAME "sysinfo_202202906"
#define MAX_CMDLINE_LENGTH 256
#define CONTAINER_ID_LENGTH 12
#define CONTAINER_PREFIX "stress_"

struct cpu_usage_data {
    char container_id[65]; // Max Docker container ID length + 1
    unsigned long last_usage_usec;
    unsigned long last_rbytes;
    unsigned long last_wbytes;
    ktime_t last_timestamp;
    struct list_head list;
};

// Función para obtener la línea de comandos de un proceso
static char *get_process_cmdline(struct task_struct *task) {

    /* 
        Creamos una estructura mm_struct para obtener la información de memoria
        Creamos un apuntador char para la línea de comandos
        Creamos un apuntador char para recorrer la línea de comandos
        Creamos variables para guardar las direcciones de inicio y fin de los argumentos y el entorno
        Creamos variables para recorrer la línea de comandos
    */
    struct mm_struct *mm;
    char *cmdline, *p;
    unsigned long arg_start, arg_end, env_start;
    int i, len;


    // Reservamos memoria para la línea de comandos
    cmdline = kmalloc(MAX_CMDLINE_LENGTH, GFP_KERNEL);
    if (!cmdline)
        return NULL;

    // Obtenemos la información de memoria
    mm = get_task_mm(task);
    if (!mm) {
        kfree(cmdline);
        return NULL;
    }

    /* 
       1. Primero obtenemos el bloqueo de lectura de la estructura mm_struct para una lectura segura
       2. Obtenemos las direcciones de inicio y fin de los argumentos y el entorno
       3. Liberamos el bloqueo de lectura de la estructura mm_struct
    */
    down_read(&mm->mmap_lock);
    arg_start = mm->arg_start;
    arg_end = mm->arg_end;
    env_start = mm->env_start;
    up_read(&mm->mmap_lock);

    // Obtenemos la longitud de la línea de comandos y validamos que no sea mayor a MAX_CMDLINE_LENGTH - 1
    len = arg_end - arg_start;

    if (len > MAX_CMDLINE_LENGTH - 1)
        len = MAX_CMDLINE_LENGTH - 1;

    // Obtenemos la línea de comandos de  la memoria virtual del proceso
    /* 
        Por qué de la memoria virtual del proceso?
        La memoria virtual es la memoria que un proceso puede direccionar, es decir, la memoria que un proceso puede acceder
    */
    if (access_process_vm(task, arg_start, cmdline, len, 0) != len) {
        mmput(mm);
        kfree(cmdline);
        return NULL;
    }

    // Agregamos un caracter nulo al final de la línea de comandos
    cmdline[len] = '\0';

    // Reemplazar caracteres nulos por espacios
    p = cmdline;
    for (i = 0; i < len; i++)
        if (p[i] == '\0')
            p[i] = ' ';

    // Liberamos la estructura mm_struct
    mmput(mm);
    return cmdline;
}

static char *get_container_id(struct task_struct *task) {
    struct cgroup *cgrp;
    char *container_id = NULL;
    char *path_buffer;
    char *docker_pos;
    char *end_pos;

    /* Asignar memoria dinámicamente para evitar stack overflow */
    path_buffer = kmalloc(PATH_MAX, GFP_KERNEL);
    if (!path_buffer)
        return NULL;

    /* Obtener el cgroup asociado */
    cgrp = task_cgroup(task, memory_cgrp_id);
    if (!cgrp) {
        kfree(path_buffer);
        return NULL;
    }

    /* Obtener la ruta del cgroup */
    if (cgroup_path(cgrp, path_buffer, PATH_MAX) <= 0) {
        kfree(path_buffer);
        return NULL;
    }

    pr_info("Cgroup path: %s\n", path_buffer);

    /* Buscar "docker-" en la ruta */
    docker_pos = strstr(path_buffer, "docker-");
    if (docker_pos) {
        docker_pos += 7; // Saltar "docker-"

        /* Buscar el final del ID del contenedor antes de ".scope" */
        end_pos = strstr(docker_pos, ".scope");
        if (end_pos) {
            *end_pos = '\0';
        }

        /* Calcular la longitud del ID completo */
        size_t id_length = strlen(docker_pos);
        if (id_length > 0) {
            /* Asignar memoria para el ID completo + terminador nulo */
            container_id = kmalloc(id_length + 1, GFP_KERNEL);
            if (container_id) {
                strncpy(container_id, docker_pos, id_length);
                container_id[id_length] = '\0'; // Asegurar terminación
            }
        }
    }

    /* Liberar memoria del buffer */
    kfree(path_buffer);
    return container_id;
}


static unsigned long get_cpu_usage(void) {
    unsigned long user, nice, system, idle;
    unsigned long total_usage, total_time;

    // Obtener datos de CPU desde kcpustat_cpu()
    user = kcpustat_cpu(0).cpustat[CPUTIME_USER];
    nice = kcpustat_cpu(0).cpustat[CPUTIME_NICE];
    system = kcpustat_cpu(0).cpustat[CPUTIME_SYSTEM];
    idle = kcpustat_cpu(0).cpustat[CPUTIME_IDLE];

    total_time = user + nice + system + idle;
    total_usage = (user + nice + system) * 10000 / total_time; // Escalado a 2 decimales

    return total_usage;
}

// Helper function to check if a process is a parent (has children)
static int is_parent_process(struct task_struct *task) {
    return !list_empty(&task->children);
}


static unsigned long get_memory_usage(const char *container_id) {
    char *path = NULL;
    struct file *filp = NULL;
    char *buf = NULL;
    loff_t pos = 0;
    ssize_t bytes_read;
    unsigned long mem_usage = 0;
    int ret;

    // Allocate memory for the path
    path = kmalloc(PATH_MAX, GFP_KERNEL);
    if (!path) {
        printk(KERN_WARNING "Failed to allocate memory for path\n");
        return 0;
    }

    // Allocate memory for the buffer (increased to 128 bytes for safety)
    buf = kmalloc(128, GFP_KERNEL);
    if (!buf) {
        printk(KERN_WARNING "Failed to allocate memory for buffer\n");
        goto free_path;
    }

    // Construct the path to the memory.current file
    snprintf(path, PATH_MAX, "/sys/fs/cgroup/system.slice/docker-%s.scope/memory.current", container_id);

    // Open the file
    filp = filp_open(path, O_RDONLY, 0);
    if (IS_ERR(filp)) {
        printk(KERN_WARNING "Failed to open %s: %ld\n", path, PTR_ERR(filp));
        goto free_buf;
    }

    // Read the memory usage
    bytes_read = kernel_read(filp, buf, 127, &pos);
    if (bytes_read > 0) {
        buf[bytes_read] = '\0'; // Ensure null termination
        ret = kstrtoul(buf, 10, &mem_usage); // Convert string to unsigned long
        if (ret < 0) {
            printk(KERN_WARNING "Failed to parse memory usage from %s\n", path);
            mem_usage = 0;
        }
    } else {
        printk(KERN_WARNING "Failed to read from %s\n", path);
    }

    // Close the file
    filp_close(filp, NULL);

    free_buf:
        kfree(buf);
    free_path:
        kfree(path);

        // Convert to MB if desired (optional)
        // mem_usage = mem_usage / (1024 * 1024); // Uncomment to convert to MB

    return mem_usage / (1024 * 1024) ;
}

static unsigned long get_cpu_usage_container(const char *container_id) {
    char *path = NULL;
    struct file *filp = NULL;
    char *buf = NULL;
    loff_t pos = 0;
    ssize_t bytes_read;
    unsigned long cpu_usage = 0;
    int ret;
    char *line, *value_start, *end;

    // Allocate memory for the path
    path = kmalloc(PATH_MAX, GFP_KERNEL);
    if (!path) {
        printk(KERN_WARNING "Failed to allocate memory for path\n");
        return 0;
    }

    // Allocate memory for the buffer
    buf = kmalloc(256, GFP_KERNEL); // Larger buffer to handle multiple lines
    if (!buf) {
        printk(KERN_WARNING "Failed to allocate memory for buffer\n");
        goto free_path;
    }

    // Construct the path to the cpu.stat file
    snprintf(path, PATH_MAX, "/sys/fs/cgroup/system.slice/docker-%s.scope/cpu.stat", container_id);

    // Open the file
    filp = filp_open(path, O_RDONLY, 0);
    if (IS_ERR(filp)) {
        printk(KERN_WARNING "Failed to open %s: %ld\n", path, PTR_ERR(filp));
        goto free_buf;
    }

    // Read the entire file content
    bytes_read = kernel_read(filp, buf, 255, &pos);
    if (bytes_read > 0) {
        buf[bytes_read] = '\0'; // Ensure null termination

        // Parse the file line by line
        line = buf;
        while ((line = strstr(line, "usage_usec")) != NULL) {
            printk(KERN_INFO "Found 'usage_usec' at line: %s\n", line); // Debug print
            value_start = line + strlen("usage_usec"); // Move past "usage_usec"
            while (*value_start == ' ' || *value_start == '\t') value_start++; // Skip whitespace
            if (*value_start == '\0' || *value_start == '\n') break; // Invalid line

            // Find the end of the number (next whitespace or newline)
            end = value_start;
            while (*end && *end != ' ' && *end != '\t' && *end != '\n') end++;
            if (end > value_start) {
                char temp[32]; // Temporary buffer for the number
                size_t len = end - value_start;
                if (len >= sizeof(temp)) len = sizeof(temp) - 1;
                strncpy(temp, value_start, len);
                temp[len] = '\0';
                ret = kstrtoul(temp, 10, &cpu_usage);
                if (ret == 0) {
                    printk(KERN_INFO "Parsed usage_usec: %lu\n", cpu_usage); // Debug print
                    break; // Successfully parsed
                }
            }
            line = end; // Move to next line or position
        }

        if (ret < 0) {
            printk(KERN_WARNING "Failed to parse cpu usage from %s\n", path);
            cpu_usage = 0;
        }
    } else {
        printk(KERN_WARNING "Failed to read from %s\n", path);
    }

    // Close the file
    filp_close(filp, NULL);

    free_buf:
        kfree(buf);
    free_path:
        kfree(path);

    return cpu_usage; // Returns usage in microseconds
}

static void get_io_stats(const char *container_id, unsigned long *read_mb, unsigned long *write_mb) {
    char *path = NULL;
    struct file *filp = NULL;
    char *buf = NULL;
    loff_t pos = 0;
    ssize_t bytes_read;
    char *line, *value_start, *end;
    unsigned long current_rbytes = 0, current_wbytes = 0;
    int ret;
    struct cpu_usage_data *data = NULL;
    ktime_t now = ktime_get();
    unsigned long elapsed_us, read_diff, write_diff;

    *read_mb = 0;
    *write_mb = 0;

    // Allocate memory for the path
    path = kmalloc(PATH_MAX, GFP_KERNEL);
    if (!path) {
        printk(KERN_WARNING "Failed to allocate memory for path\n");
        return;
    }

    // Allocate memory for the buffer
    buf = kmalloc(512, GFP_KERNEL); // Larger buffer for io.stat
    if (!buf) {
        printk(KERN_WARNING "Failed to allocate memory for buffer\n");
        goto free_path;
    }

    // Construct the path to the io.stat file
    snprintf(path, PATH_MAX, "/sys/fs/cgroup/system.slice/docker-%s.scope/io.stat", container_id);

    // Open the file
    filp = filp_open(path, O_RDONLY, 0);
    if (IS_ERR(filp)) {
        printk(KERN_WARNING "Failed to open %s: %ld\n", path, PTR_ERR(filp));
        goto free_buf;
    }

    // Read the entire file content
    bytes_read = kernel_read(filp, buf, 511, &pos);
    if (bytes_read > 0) {
        buf[bytes_read] = '\0';

        // Parse the file for rbytes and wbytes
        line = buf;
        while (line && *line) {
            if ((value_start = strstr(line, "rbytes=")) != NULL) {
                value_start += strlen("rbytes=");
                end = value_start;
                while (*end && *end != ' ' && *end != '\n') end++;
                if (end > value_start) {
                    char temp[32];
                    size_t len = end - value_start;
                    if (len >= sizeof(temp)) len = sizeof(temp) - 1;
                    strncpy(temp, value_start, len);
                    temp[len] = '\0';
                    ret = kstrtoul(temp, 10, &current_rbytes);
                    if (ret < 0) current_rbytes = 0;
                }
            }
            if ((value_start = strstr(line, "wbytes=")) != NULL) {
                value_start += strlen("wbytes=");
                end = value_start;
                while (*end && *end != ' ' && *end != '\n') end++;
                if (end > value_start) {
                    char temp[32];
                    size_t len = end - value_start;
                    if (len >= sizeof(temp)) len = sizeof(temp) - 1;
                    strncpy(temp, value_start, len);
                    temp[len] = '\0';
                    ret = kstrtoul(temp, 10, &current_wbytes);
                    if (ret < 0) current_wbytes = 0;
                }
            }
            line = strstr(line, "\n");
            if (line) line++;
        }
    } else {
        printk(KERN_WARNING "Failed to read from %s\n", path);
    }

    filp_close(filp, NULL);

    // Lock to safely access the list
    mutex_lock(&cpu_usage_lock);

    // Find or create usage data for this container
    list_for_each_entry(data, &cpu_usage_list, list) {
        if (strcmp(data->container_id, container_id) == 0) {
            break;
        }
    }
    if (!data) {
        data = kmalloc(sizeof(*data), GFP_KERNEL);
        if (data) {
            strncpy(data->container_id, container_id, sizeof(data->container_id) - 1);
            data->container_id[sizeof(data->container_id) - 1] = '\0';
            data->last_usage_usec = 0;
            data->last_rbytes = 0;
            data->last_wbytes = 0;
            data->last_timestamp = ktime_set(0, 0);
            INIT_LIST_HEAD(&data->list);
            list_add(&data->list, &cpu_usage_list);
        }
    }

    if (data) {
        elapsed_us = ktime_us_delta(now, data->last_timestamp);
        if (elapsed_us > 0) {
            read_diff = current_rbytes - data->last_rbytes;
            write_diff = current_wbytes - data->last_wbytes;
            // Convert to MB per second (approximate)
            if (elapsed_us > 0) {
                *read_mb = (read_diff / (1024 * 1024)) * (1000000 / elapsed_us); // MB/s scaled by 1000000
                *write_mb = (write_diff / (1024 * 1024)) * (1000000 / elapsed_us); // MB/s scaled by 1000000
            }
        } else {
            *read_mb = 0;
            *write_mb = 0;
        }
        data->last_rbytes = current_rbytes;
        data->last_wbytes = current_wbytes;
        data->last_timestamp = now;
    }

    mutex_unlock(&cpu_usage_lock);

    free_buf:
        kfree(buf);
    free_path:
        kfree(path);
}

static int sysinfo_show(struct seq_file *m, void *v) {
    struct sysinfo si;
    struct task_struct *task;
    unsigned long total_jiffies = jiffies;
    int first_process = 1;
    unsigned long cpu_usage_total;
    char *seen_containers[150] = {0}; // Simple array to track seen container IDs
    int container_count = 0;

    si_meminfo(&si);
    cpu_usage_total = get_cpu_usage();

    seq_printf(m, "{\n");
    seq_printf(m, "  \"Memory\": {\n");
    seq_printf(m, "    \"Total_Memory_MB\": %lu,\n", si.totalram * 4 / 1024);
    seq_printf(m, "    \"Free_Memory_MB\": %lu,\n", si.freeram * 4 / 1024);
    seq_printf(m, "    \"Used_Memory_MB\": %lu,\n", (si.totalram - si.freeram) * 4 / 1024);
    seq_printf(m, "    \"CPU_Usage_Percentage\": %lu.%02lu\n", cpu_usage_total / 100, cpu_usage_total % 100);
    seq_printf(m, "  },\n");

    seq_printf(m, "  \"Docker_Containers\": [\n");

    for_each_process(task) {
        if (strcmp(task->comm, "stress") == 0 && is_parent_process(task)) { // Check if it's a parent process
            char *containerID = get_container_id(task);
            char *cmdline = NULL;
            int already_seen = 0;

            // Skip if container ID is already processed
            if (containerID) {
                for (int i = 0; i < container_count; i++) {
                    if (seen_containers[i] && strcmp(seen_containers[i], containerID) == 0) {
                        already_seen = 1;
                        break;
                    }
                }
            }

            if (!already_seen && containerID) {
                unsigned long vsz = 0, rss = 0, mem_usage = 0, mem_percentage = 0;
                unsigned long disk_read_mb = 0, disk_write_mb = 0;
                unsigned long io_read = 0, io_write = 0;

                if (task->mm) {
                    vsz = task->mm->total_vm << (PAGE_SHIFT - 10); // In KB, then MB
                    rss = get_mm_rss(task->mm) << (PAGE_SHIFT - 10); // In KB, then MB
                    mem_usage = get_memory_usage(containerID); // In MB from cgroup
                    unsigned long total_memory_mb = si.totalram * 4 / 1024; // Total RAM in MB
                    mem_percentage = (mem_usage * 10000) / total_memory_mb;
                }

                mem_usage = get_memory_usage(containerID);

                unsigned long cpu_usage_us = get_cpu_usage_container(containerID);
                unsigned long total_system_us = jiffies_to_usecs(jiffies); // Total system uptime in microseconds
                unsigned long total_cpu_time_us = total_system_us; // Total available CPU time across all cores
                unsigned long cpu_percentage;
            
                if (total_cpu_time_us > 0) {
                    cpu_percentage = (cpu_usage_us * 10000) / total_cpu_time_us; // Scale to 2 decimal places
                } else {
                    cpu_percentage = 0;
                }

                cmdline = get_process_cmdline(task);

                unsigned long read_mb, write_mb;
                get_io_stats(containerID, &read_mb, &write_mb);
                io_read = task->ioac.read_bytes;
                io_write = task->ioac.write_bytes;

                if (!first_process) {
                    seq_printf(m, ",\n");
                } else {
                    first_process = 0;
                }

                seq_printf(m, "    {\n");
                seq_printf(m, "      \"PID\": %d,\n", task->pid);
                seq_printf(m, "      \"Name\": \"%s\",\n", task->comm);
                seq_printf(m, "      \"ContainerID\": \"%s\",\n", containerID ? containerID : "N/A");
                seq_printf(m, "      \"Cmdline\": \"%s\",\n", cmdline ? cmdline : "N/A");
                seq_printf(m, "      \"MemoryUsage\": %lu.%02lu,\n", mem_percentage / 100, mem_percentage % 100);
                seq_printf(m, "      \"MEMORY\": %lu,\n", mem_usage);
                seq_printf(m, "      \"CPUUsage\": %lu.%02lu,\n", cpu_percentage / 100, cpu_percentage % 100);
                seq_printf(m, "      \"DiskReadMB\": %lu,\n", read_mb);
                seq_printf(m, "      \"DiskWriteMB\": %lu,\n", write_mb);
                seq_printf(m, "      \"IORead\": %lu,\n", io_read);
                seq_printf(m, "      \"IOWrite\": %lu\n", io_write);
                seq_printf(m, "    }");

                // Store the container ID to avoid duplicates
                if (container_count < 128) {
                    seen_containers[container_count++] = containerID;
                } else {
                    kfree(containerID); // Free if we exceed the array size
                }
            } else {
                if (containerID) kfree(containerID);
            }

            if (cmdline) {
                kfree(cmdline);
            }
        }
    }

    seq_printf(m, "\n  ]\n");
    seq_printf(m, "}\n");

    // Clean up seen_containers
    for (int i = 0; i < container_count; i++) {
        if (seen_containers[i]) kfree(seen_containers[i]);
    }

    return 0;
}


// Función que se ejecuta al abrir el archivo /proc
static int sysinfo_open(struct inode *inode, struct file *file) {
    return single_open(file, sysinfo_show, NULL);
}

// Estructura de operaciones del archivo /proc
static const struct proc_ops sysinfo_ops = {
    .proc_open = sysinfo_open,
    .proc_read = seq_read,
};

// Inicialización del módulo
static int __init sysinfo_init(void) {
    proc_create(PROC_NAME, 0, NULL, &sysinfo_ops);
    printk(KERN_INFO "sysinfo_202202906: Módulo cargado\n");
    return 0;
}

// Eliminación del módulo
static void __exit sysinfo_exit(void) {
    remove_proc_entry(PROC_NAME, NULL);
    printk(KERN_INFO "sysinfo_202202906: Módulo descargado\n");
}

module_init(sysinfo_init);
module_exit(sysinfo_exit);