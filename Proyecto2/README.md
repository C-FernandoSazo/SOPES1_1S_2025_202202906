## Comandos

Ver redis
kubectl exec -it -n proyecto2 $(kubectl get pod -n proyecto2 -l app=redis -o name) -- redis-cli

Ver Valkey
kubectl exec -it -n proyecto2 $(kubectl get pod -n proyecto2 -l app=valkey -o name) -- valkey-cli



curl -X POST http://localhost:8080/publicar -H "Content-Type: application/json" -d '{"description":"Nevado","country":"Andorra","weather":"7°C"}'

Ver discos volumenes persistentes del cluster
gcloud compute disks list --project brave-frame-455000-p8

Para contadores en los consumers buscar "HINCRBY"

Escuchar puerto
kubectl port-forward svc/goclient 8080:8080 -n proyecto2


curl -X POST http://34.29.212.164.nip.io/input -H "Content-Type: application/json" -d '{"description":"Soleado","country":"España","weather":"25°C"}'

activar locust

locust -f locustfile.py