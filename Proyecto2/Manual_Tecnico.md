# Documentación de Deployments – Proyecto2 (Actualizada)

Este documento detalla los deployments y recursos relacionados del sistema **Proyecto2**, un sistema distribuido desplegado en **Kubernetes** (`namespace: proyecto2`). El sistema recibe mensajes climáticos a través de una API REST en Rust, los procesa mediante una API en Go (`goclient`), los publica en sistemas de mensajería (**Kafka** y **RabbitMQ**), los consume para almacenarlos en bases clave‑valor (**Redis** y **Valkey**), y finalmente los visualiza en un dashboard de **Grafana**. El objetivo es procesar y visualizar datos climáticos de manera eficiente y escalable.

- **Fecha:** 21 de abril de 2025  
- **Namespace:** `proyecto2`

---

## Arquitectura general

1. **Ingress (`34.29.212.164.nip.io`)** – Recibe peticiones HTTP desde clientes externos (como *Locust* para pruebas de carga) y las redirige a la API en Rust.  
2. **Rust API (`rust-api`)** – Punto de entrada que recibe mensajes climáticos en formato JSON y los reenvía al servicio `goclient`.  
3. **Go Client (`goclient`)** – Procesa los mensajes y los publica simultáneamente en **Kafka** y **RabbitMQ**.  
4. **Sistemas de mensajería**  
   - **Kafka** (gestionado por *Strimzi*), topic `clima-topic`  
   - **RabbitMQ** (StatefulSet), cola `clima-queue`  
5. **Consumidores**  
   - `kafka-consumer` – Lee mensajes de Kafka y los almacena en **Redis** para su posterior visualización. 
   - `rabbit-consumer` – Lee mensajes de RabbitMQ y los almacena en **Valkey** para su posterior visualización. 

6. **Almacenamiento**  
   - **Redis** – Datos desde Kafka  
   - **Valkey** – Datos desde RabbitMQ  
7. **Visualización**  
   - **Grafana** – Dashboard interactivo con los datos de Redis y Valkey

---

## Lista de Deployments

### 1. Rust API (`rust-api`)

**Descripción:** Una API REST desarrollada en Rust utilizando el framework Actix Web, diseñada para recibir peticiones **HTTP POST** en la ruta`/input`. Reenvía los mensajes climático‑JSON al servicio `goclient`. Está configurada para soportar alta carga y escalar horizontalmente mediante un Horizontal Pod Autoscaler (HPA). 

- **Imagen:** `35.223.156.111:443/proyecto2/rust-api:latest`
- **Archivo:** `~/…/cluster/rust/rust-api.yaml`

**Deployment**
```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: rust-api
  namespace: proyecto2
spec:
  replicas: 1
  selector:
    matchLabels:
      app: rust-api
  template:
    metadata:
      labels:
        app: rust-api
    spec:
      containers:
      - name: rust-api
        image: 35.223.156.111:443/proyecto2/rust-api:latest
        ports:
        - containerPort: 8080
        resources:
          requests:
            memory: "64Mi"
            cpu: "100m"
          limits:
            memory: "256Mi"
            cpu: "500m"
```

**Service**
```yaml
apiVersion: v1
kind: Service
metadata:
  name: rust-api
  namespace: proyecto2
spec:
  selector:
    app: rust-api
  ports:
  - port: 8080
    targetPort: 8080
    protocol: TCP
  type: ClusterIP
```

**HPA (Horizontal Pod Autoscaler)**
```yaml
apiVersion: autoscaling/v2
kind: HorizontalPodAutoscaler
metadata:
  name: rust-api-hpa
  namespace: proyecto2
spec:
  scaleTargetRef:
    apiVersion: apps/v1
    kind: Deployment
    name: rust-api
  minReplicas: 1
  maxReplicas: 3
  metrics:
  - type: Resource
    resource:
      name: cpu
      target:
        type: Utilization
        averageUtilization: 30
```


**Ingress**
**Descripción:** Permite todo el trafico de red por medio de la IP publica `34.29.212.164.nip.io` y el endpoint `/input`.
```yaml
apiVersion: networking.k8s.io/v1
kind: Ingress
metadata:
  name: rust-api-ingress
  namespace: proyecto2
spec:
  ingressClassName: nginx
  rules:
  - host: 34.29.212.164.nip.io
    http:
      paths:
      - path: /input
        pathType: Prefix
        backend:
          service:
            name: rust-api
            port:
              number: 8080
```

---

### 2. Go Client (`goclient`)

**Descripción:** Una API desarrollada en Go que actúa como intermediario entre la **API en Rust** y los sistemas de mensajería **(Kafka y RabbitMQ)**. Recibe mensajes climáticos desde `rust-api` mediante HTTP POST en la ruta `/publicar`, y los publica simultáneamente en **Kafka** (topic `clima-topic`) y **RabbitMQ** (cola `clima-queue`). Devuelve una respuesta JSON indicando el éxito o fallo de la publicación.
- **Imagen:** `35.223.156.111:443/proyecto2/goclient:latest`

**Deployment**
```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: goclient
  namespace: proyecto2
spec:
  replicas: 1
  selector:
    matchLabels:
      app: goclient
  template:
    metadata:
      labels:
        app: goclient
    spec:
      containers:
      - name: goclient
        image: 35.223.156.111:443/proyecto2/goclient:latest
        ports:
        - containerPort: 8080
```

**Service**
```yaml
apiVersion: v1
kind: Service
metadata:
  name: goclient
  namespace: proyecto2
spec:
  selector:
    app: goclient
  ports:
  - port: 8080
    targetPort: 8080
    protocol: TCP
  type: ClusterIP
```

---

### 3. Kafka Writer (`kafka-writer`)

**Descripción:** Servicio gRPC en Go que recibe solicitudes de `goclient` (puerto `50052`) y publica los mensajes en **Kafka**.

- **Imagen:** `35.223.156.111:443/proyecto2/kafka-writer:latest`

**Deployment**
```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: kafka-writer
  namespace: proyecto2
spec:
  replicas: 1
  selector:
    matchLabels:
      app: kafka-writer
  template:
    metadata:
      labels:
        app: kafka-writer
    spec:
      containers:
      - name: kafka-writer
        image: 35.223.156.111:443/proyecto2/kafka-writer:latest
        ports:
        - containerPort: 50052
```

**Service**
```yaml
apiVersion: v1
kind: Service
metadata:
  name: kafka-writer
  namespace: proyecto2
spec:
  selector:
    app: kafka-writer
  ports:
  - port: 50052
    targetPort: 50052
    protocol: TCP
  type: ClusterIP
```

---

### 4. Kafka Consumer (`kafka-consumer`)

**Descripción:** Consumidor desarrollado en Go que lee de `clima-topic` en Kafka y almacena los mensajes en **Redis**.  
Cada mensaje climático es almacenado como un hash en Redis con claves como `clima:<country>:<timestamp>`, y se actualizan contadores (`country_counts` y `total_messages`) para su visualización en **Grafana**.

- **Imagen:** `35.223.156.111:443/proyecto2/kafka-consumer:latest`
- **Notas:** 2 réplicas para procesamiento paralelo

**Deployment**
```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: kafka-consumer
  namespace: proyecto2
spec:
  replicas: 2
  selector:
    matchLabels:
      app: kafka-consumer
  template:
    metadata:
      labels:
        app: kafka-consumer
    spec:
      containers:
      - name: kafka-consumer
        image: 35.223.156.111:443/proyecto2/kafka-consumer:latest
        env:
        - name: GROUP_INSTANCE_ID
          value: "consumer-$POD_NAME"
```

---

### 5. Rabbit Writer (`rabbit-writer`)

**Descripción:** Servicio gRPC en Go que recibe solicitudes de `goclient` (puerto `50051`) y los publica en la cola `clima-queue` de **RabbitMQ**.

- **Imagen:** `35.223.156.111:443/proyecto2/rabbit-writer:latest`

**Deployment**
```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: rabbit-writer
  namespace: proyecto2
spec:
  replicas: 1
  selector:
    matchLabels:
      app: rabbit-writer
  template:
    metadata:
      labels:
        app: rabbit-writer
    spec:
      containers:
      - name: rabbit-writer
        image: 35.223.156.111:443/proyecto2/rabbit-writer:latest
```

**Service**
```yaml
apiVersion: v1
kind: Service
metadata:
  name: rabbit-writer
  namespace: proyecto2
spec:
  selector:
    app: rabbit-writer
  ports:
  - port: 5672
    targetPort: 5672
    protocol: TCP
  type: ClusterIP
```

---

### 6. Rabbit Consumer (`rabbit-consumer`)

**Descripción:** Un consumidor desarrollado en Go que lee mensajes de la cola `clima-queue` en **RabbitMQ** y los almacena en **Valkey**. Cada mensaje climático es almacenado como un hash en Valkey con claves como `clima:<country>:<timestamp>`, y se actualizan contadores (`country_counts` y `total_messages`) para su visualización. Utiliza una sola réplica para evitar competencia en la cola. 
**Imagen:** `35.223.156.111:443/proyecto2/rabbit-consumer:latest`

**Deployment**
```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: rabbit-consumer
  namespace: proyecto2
spec:
  replicas: 1
  selector:
    matchLabels:
      app: rabbit-consumer
  template:
    metadata:
      labels:
        app: rabbit-consumer
    spec:
      containers:
      - name: rabbit-consumer
        image: 35.223.156.111:443/proyecto2/rabbit-consumer:latest
```

---

### 7. Redis (`redis`)

**Descripción:** Un sistema de almacenamiento **clave-valor** que guarda los datos provenientes de **Kafka**, procesados por `kafka-consumer`. 

**Deployment**
```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: redis
  namespace: proyecto2
spec:
  replicas: 1
  selector:
    matchLabels:
      app: redis
  template:
    metadata:
      labels:
        app: redis
    spec:
      containers:
      - name: redis
        image: redis:latest
        ports:
        - containerPort: 6379
        volumeMounts:
        - name: redis-data
          mountPath: /data
      volumes:
      - name: redis-data
        persistentVolumeClaim:
          claimName: redis-pvc
```

**PVC**
```yaml
apiVersion: v1
kind: PersistentVolumeClaim
metadata:
  name: redis-pvc
  namespace: proyecto2
spec:
  accessModes:
  - ReadWriteOnce
  resources:
    requests:
      storage: 2Gi
  storageClassName: standard
```

**Service**
```yaml
apiVersion: v1
kind: Service
metadata:
  name: redis
  namespace: proyecto2
spec:
  selector:
    app: redis
  ports:
  - port: 6379
    targetPort: 6379
    protocol: TCP
  type: ClusterIP
```

---

### 8. Valkey (`valkey`)

**Descripción:** Un sistema de almacenamiento **clave-valor** (fork de Redis) que guarda los datos provenientes de **RabbitMQ**, procesados por `rabbit-consumer`.

**Deployment**
```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: valkey
  namespace: proyecto2
spec:
  replicas: 1
  selector:
    matchLabels:
      app: valkey
  template:
    metadata:
      labels:
        app: valkey
    spec:
      containers:
      - name: valkey
        image: valkey/valkey:latest
        ports:
        - containerPort: 6379
        volumeMounts:
        - name: valkey-data
          mountPath: /data
      volumes:
      - name: valkey-data
        persistentVolumeClaim:
          claimName: valkey-pvc
```

**PVC**
```yaml
apiVersion: v1
kind: PersistentVolumeClaim
metadata:
  name: valkey-pvc
  namespace: proyecto2
spec:
  accessModes:
  - ReadWriteOnce
  resources:
    requests:
      storage: 2Gi
  storageClassName: standard
```

**Service**
```yaml
apiVersion: v1
kind: Service
metadata:
  name: valkey
  namespace: proyecto2
spec:
  selector:
    app: valkey
  ports:
  - port: 6379
    targetPort: 6379
    protocol: TCP
  type: ClusterIP
```

---

### 9. Grafana (`grafana`)

**Descripción:** Herramienta de visualización para los datos de Redis y Valkey mediante el `Clima Dashboard`, mostrando datos como la cantidad **total** de mensajes, y la cantidad de mensajes por paises.

**Parametros personalizados**
```yaml
persistence:
  enabled: true
  storageClassName: standard
  accessModes:
    - ReadWriteOnce
  size: 3Gi

service:
  type: LoadBalancer
  port: 80
  targetPort: 3000

adminPassword: cesarFer123

datasources:
  datasources.yaml:
    apiVersion: 1
    datasources:
    - name: Redis
      type: redis-datasource
      access: proxy
      url: redis.proyecto2.svc.cluster.local:6379
      isDefault: false
    - name: Valkey
      type: redis-datasource
      access: proxy
      url: valkey.proyecto2.svc.cluster.local:6379
      isDefault: false
```

```bash
helm install grafana grafana/grafana -f ./grafana-values.yaml -n proyecto2
```

---

## Recursos adicionales

### Kafka (Strimzi)

- **Broker**: `my-cluster-kafka-0`

- **Zookeeper**: `my-cluster-zookeeper-0`

- **Operador**: `strimzi-cluster-operator`

- **Topic**: `clima-topic`

```yaml
apiVersion: kafka.strimzi.io/v1beta2
kind: KafkaTopic
metadata:
  name: clima-topic
  namespace: proyecto2
  labels:
    strimzi.io/cluster: my-cluster
spec:
  partitions: 2
  replicas: 1
  config:
    retention.ms: 604800000
```

### RabbitMQ

- StatefulSet: `clima-rabbit-server-0`  
- Usuario: `default_user_OHcC7hSMN9bwkYmR9gc`  
- Contraseña: `EFGBxZb_RpQ8I8vjeDMUETWZHJnoPU-w`  
- Cola: `clima-queue`

---

## Comandos útiles

```bash
# Pods
kubectl get pods -n proyecto2

# Servicios
kubectl get svc -n proyecto2

# Ingress
kubectl get ingress -n proyecto2

# Reiniciar un Deployment
kubectl rollout restart deployment <deployment-name> -n proyecto2

# Ver logs
kubectl logs -l app=<app-name> -n proyecto2

# Ejecución locust
locust -f locustfile.py
```

---

# Respuestas a las Preguntas

## 1. ¿Cómo funciona Kafka?
Kafka es un sistema de mensajería distribuido diseñado para manejar grandes volúmenes de datos en tiempo real. Funciona como una plataforma de streaming de eventos basada en un modelo de publicación-suscripción.

### Conceptos clave:
- **Topics**: Categorías o canales donde los productores publican mensajes y los consumidores los leen (ej. `clima-topic`).
- **Partitions**: Cada topic se divide en particiones para procesamiento paralelo (ej. `clima-topic` tiene 2 particiones).
- **Brokers**: Servidores de Kafka que almacenan y gestionan los mensajes (ej. `my-cluster-kafka-0`).
- **Producers y Consumers**: Publican y consumen mensajes. Pueden formar grupos para distribuir la carga (ej. `kafka-writer` y `kafka-consumer`).
- **Offsets**: Identificador secuencial de cada mensaje dentro de una partición.

### Funcionamiento:
1. El productor publica un mensaje en un topic.
2. El broker almacena el mensaje en una partición.
3. Los consumidores leen los mensajes, uno por partición si están en un grupo.
4. Los mensajes se retienen por un tiempo configurado (`retention.ms`).

---

## 2. ¿Cómo difiere Valkey de Redis?
Valkey es un fork de Redis creado en 2024 tras el cambio de licencia de Redis.

### Diferencias clave:
- **Licencia**:
  - *Redis*: Licencia dual (RSALv2/SSPL), más restrictiva.
  - *Valkey*: BSD-3-Clause, más abierta.

- **Comunidad**:
  - *Redis*: Gestionado por Redis Inc.
  - *Valkey*: Respaldado por Linux Foundation, AWS, Google, Oracle.

- **Funcionalidad**:
  - Ambas son funcionalmente idénticas actualmente (Valkey se basa en Redis 7.2.4).

### En nuestro caso:
- Redis se usó para Kafka.
- Valkey para RabbitMQ.
- Ambos almacenan hashes y contadores `clima:*`.

---

## 3. ¿Es mejor gRPC que HTTP?
Depende del caso de uso.

### Comparación:
**gRPC**:
- Usa Protocol Buffers (más eficiente que JSON).
- Soporta *streaming* bidireccional.
- Genera código para múltiples lenguajes.
- Ideal para servicios internos (baja latencia).

**HTTP**:
- Simple y universal.
- Compatible con navegadores y herramientas comunes.
- Ideal para APIs públicas o externas.

### En nuestro caso:
- Usaste HTTP en `rust-api` y `goclient` (entrada externa).
- Usaste gRPC entre `goclient` y `kafka-writer` (eficiencia interna).

**Conclusión**: gRPC es mejor para comunicación interna, HTTP para entrada externa.

---

## 4. ¿Hubo mejora al usar dos réplicas en los deployments?

### API REST (`rust-api`):
- Tiene HPA (Horizontal Pod Autoscaler) de 1 a 3 réplicas.
- Mejora observada:
  - Mayor capacidad de procesamiento simultáneo.
  - Menor latencia.
  - Mayor tolerancia a fallos.

### gRPC (`kafka-writer`):
- Actualmente tiene una sola réplica.
- Se beneficiaría de una segunda réplica:
  - Mayor concurrencia.
  - Menor latencia en publicaciones.
  - Mayor resiliencia.

### Comparación con `rabbit-consumer`:
- `rabbit-consumer` tiene una sola réplica.
- RabbitMQ entrega mensajes a un consumidor por cola, más eficiente con una sola instancia.
- `kafka-consumer` tiene dos réplicas para aprovechar las particiones.

**Conclusión**:
- Kafka: 2 réplicas = procesamiento paralelo pero ligera latencia por coordinación.
- RabbitMQ: 1 réplica = menor latencia, flujo directo.

---

## 5. Consumidores utilizados y razones

### `kafka-consumer`
- **Tecnología**: Go + `confluent-kafka-go`
- **Configuración**:
  - Topic: `clima-topic`
  - Grupo: `kafka-consumer-group`
  - 2 réplicas (una por partición)
  - Static membership (`GROUP_INSTANCE_ID`)
  - Guarda datos en Redis

- **Razones**:
  - Go: eficiente y concurrente.
  - `confluent-kafka-go`: robusto.
  - Particiones = procesamiento paralelo
  - Membership estática reduce rebalanceos

### `rabbit-consumer`
- **Tecnología**: Go + `amqp`
- **Configuración**:
  - Cola: `clima-queue`
  - 1 réplica
  - Guarda datos en Valkey

- **Razones**:
  - RabbitMQ entrega mensajes a un solo consumidor por cola
  - 1 réplica evita competencia y latencia
  - Se usó Valkey para comparar con Redis



