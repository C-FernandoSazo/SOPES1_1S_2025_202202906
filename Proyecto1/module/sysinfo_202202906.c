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

MODULE_LICENSE("GPL");
MODULE_AUTHOR("202202906");
MODULE_DESCRIPTION("Módulo para monitorear memoria, CPU y procesos Docker");
MODULE_VERSION("1.0");

#define PROC_NAME "sysinfo_202202906"
#define MAX_CMDLINE_LENGTH 256
#define CONTAINER_ID_LENGTH 12
#define CONTAINER_PREFIX "stress_"

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
    char *docker_pos = strstr(path_buffer, "docker-");
    if (docker_pos) {
        docker_pos += 7; // Saltar "docker-"

        /* Buscar el final del ID del contenedor antes de ".scope" */
        char *end_pos = strstr(docker_pos, ".scope");
        if (end_pos) {
            *end_pos = '\0';
        }

        /* Recortar el ID a 12 caracteres */
        container_id = kmalloc(CONTAINER_ID_LENGTH + 1, GFP_KERNEL);
        if (container_id) {
            strncpy(container_id, docker_pos, CONTAINER_ID_LENGTH);
            container_id[CONTAINER_ID_LENGTH] = '\0'; // Asegurar terminación
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

// Función para mostrar la información en /proc/sysinfo_202202906 en JSON
static int sysinfo_show(struct seq_file *m, void *v) {
    struct sysinfo si;
    struct task_struct *task;
    unsigned long total_jiffies = jiffies;
    int first_process = 1;
    unsigned long cpu_usage_total;

    si_meminfo(&si);
    cpu_usage_total = get_cpu_usage(); // Obtener el uso total de CPU

    seq_printf(m, "{\n");
    seq_printf(m, "  \"Memory\": {\n");
    seq_printf(m, "    \"Total_Memory_MB\": %lu,\n", si.totalram * 4 / 1024);
    seq_printf(m, "    \"Free_Memory_MB\": %lu,\n", si.freeram * 4 / 1024);
    seq_printf(m, "    \"Used_Memory_MB\": %lu,\n", (si.totalram - si.freeram) * 4 / 1024);
    seq_printf(m, "    \"CPU_Usage_Percentage\": %lu.%02lu\n", cpu_usage_total / 100, cpu_usage_total % 100);
    seq_printf(m, "  },\n");

    seq_printf(m, "  \"Docker_Containers\": [\n");

    for_each_process(task) {
        if (strcmp(task->comm, "stress") == 0) {
            unsigned long vsz = 0, rss = 0, totalram, mem_usage = 0, cpu_usage = 0;
            unsigned long disk_read_mb = 0, disk_write_mb = 0;
            unsigned long io_read = 0, io_write = 0;
            char *cmdline = NULL;
            char *containerID = NULL;

            totalram = si.totalram * 4;
            if (task->mm) {
                vsz = task->mm->total_vm << (PAGE_SHIFT - 10);
                rss = get_mm_rss(task->mm) << (PAGE_SHIFT - 10);
                mem_usage = (rss * 10000) / totalram;
            }

            unsigned long total_time = task->utime + task->stime;
            cpu_usage = (total_time * 10000) / total_jiffies;
            cmdline = get_process_cmdline(task);
            containerID = get_container_id(task);

            // Obtener estadísticas de I/O
            disk_read_mb = task->ioac.read_bytes / (1024 * 1024); // Convertir a MB
            disk_write_mb = task->ioac.write_bytes / (1024 * 1024); // Convertir a MB
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
            seq_printf(m, "      \"Cmdline\": \"%s\",\n", cmdline ? cmdline : "N/A");
            seq_printf(m, "      \"ContainerID\": \"%s\",\n", containerID ? containerID : "N/AA");
            seq_printf(m, "      \"MemoryUsage\": %lu.%02lu,\n", mem_usage / 100, mem_usage % 100);
            seq_printf(m, "      \"CPUUsage\": %lu.%02lu,\n", cpu_usage / 100, cpu_usage % 100);
            seq_printf(m, "      \"DiskReadMB\": %lu,\n", disk_read_mb);
            seq_printf(m, "      \"DiskWriteMB\": %lu,\n", disk_write_mb);
            seq_printf(m, "      \"IORead\": %lu,\n", io_read);
            seq_printf(m, "      \"IOWrite\": %lu\n", io_write);
            seq_printf(m, "    }");

            if (cmdline) {
                kfree(cmdline);
            }
            if (containerID) {
                kfree(containerID);
            }
        }
    }

    seq_printf(m, "\n  ]\n");
    seq_printf(m, "}\n");

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