from locust import HttpUser, task, between
import random

class WeatherUser(HttpUser):
    wait_time = between(1, 2)

    # Lista de [Weather, Description]
    weather_data = [
        ["Lluvioso", "Está lloviendo"],
        ["Nubloso", "Cielo parcialmente nublado"],
        ["Soleado", "Soleado con temperaturas altas"],
        ["Tormenta eléctrica", "Tormenta intensa con rayos"],
        ["Granizo", "Granizo intermitente"],
        ["Viento fuerte", "Vientos huracanados"],
        ["Niebla", "Niebla densa por la mañana"],
        ["Nevado", "Nevadas leves en la tarde"],
        ["Húmedo", "Ambiente muy húmedo y cálido"],
        ["Seco", "Ambiente seco y soleado"]
    ]

    country_options = [
        "Guatemala",
        "España",
        "Brasil",
        "Argentina",
        "Colombia",
        "Italia",
        "Alemania",
        "Francia",
        "Canadá",
        "Austria",
        "Inglaterra",
        "Japón",
        "Noruega"
    ]

    @task
    def send_weather_data(self):
        weather, description = random.choice(self.weather_data)
        payload = {
            "description": description,
            "country": random.choice(self.country_options),
            "weather": weather
        }
        self.client.post("/input", json=payload)
