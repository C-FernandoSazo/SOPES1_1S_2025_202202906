use actix_web::{post, web, App, HttpResponse, HttpServer, Responder};
use reqwest::Client;
use serde::{Deserialize, Serialize};
use std::error::Error;

#[derive(Serialize, Deserialize)]
struct ClimaMessage {
    description: String,
    country: String,
    weather: String,
}

#[post("/input")]
async fn publish_clima(
    message: web::Json<ClimaMessage>,
    client: web::Data<Client>,
) -> impl Responder {
    let goclient_url = "http://goclient.proyecto2.svc.cluster.local:8080/publicar";
    
    match client
        .post(goclient_url)
        .json(&message.0)
        .send()
        .await
    {
        Ok(response) => {
            if response.status().is_success() {
                HttpResponse::Ok().json(serde_json::json!({
                    "success": true,
                    "message": "Mensaje enviado al goclient con éxito"
                }))
            } else {
                HttpResponse::InternalServerError().json(serde_json::json!({
                    "success": false,
                    "message": "Error al enviar al goclient"
                }))
            }
        }
        Err(_) => HttpResponse::InternalServerError().json(serde_json::json!({
            "success": false,
            "message": "Error conectando al goclient"
        })),
    }
}

#[actix_web::main]
async fn main() -> Result<(), Box<dyn Error>> {
    println!("Iniciando API REST en Rust en puerto 8080...");
    let client = Client::new();
    
    HttpServer::new(move || {
        App::new()
            .app_data(web::Data::new(client.clone()))
            .service(publish_clima)
    })
    .bind("0.0.0.0:8080")?
    .run()
    .await?;

    Ok(())
}