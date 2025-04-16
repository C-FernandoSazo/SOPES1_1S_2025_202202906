# Implementacion Rabbit

kubectl apply -f "https://github.com/rabbitmq/cluster-operator/releases/latest/download/cluster-operator.yml"

kubectl get all -n rabbitmq-system

kubectl apply -f rabbitmq-cluster.yaml -n proyecto2

kubectl get rabbitmqclusters -n proyecto2