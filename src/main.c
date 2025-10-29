#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <mosquitto.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#define SENSOR_INTERVAL_MS 2000

QueueHandle_t mqttQueue;

// Estrutura de mensagem
typedef struct {
    char topic[64];
    char payload[128];
} MqttMessage;

// Simula leitura de um sensor (ex: temperatura)
float read_sensor() {
    return (rand() % 3000) / 100.0f;
}

// ===================== TAREFA DO SENSOR =====================
void vSensorTask(void *pvParameters) {
    printf("[SensorTask] Iniciada.\n");
    fflush(stdout);

    while (1) {
        float value = read_sensor();
        MqttMessage msg;

        snprintf(msg.topic, sizeof(msg.topic), "v1/devices/me/telemetry");
        snprintf(msg.payload, sizeof(msg.payload),
                 "{\"temperature\": %.2f}", value);

        if (xQueueSend(mqttQueue, &msg, portMAX_DELAY) == pdPASS) {
            printf("[SensorTask] Valor lido: %.2f\n", value);
            fflush(stdout);
        } else {
            printf("[SensorTask] Erro ao enviar para fila MQTT!\n");
            fflush(stdout);
        }

        vTaskDelay(pdMS_TO_TICKS(SENSOR_INTERVAL_MS));
    }
}

// ===================== TAREFA MQTT =====================
static void on_connect(struct mosquitto *mosq, void *obj, int rc)
{
    int *connected = (int *)obj;
    if (rc == 0) {
        *connected = 1;
        printf("[MQTT] on_connect: conectado com sucesso.\n");
    } else {
        *connected = 0;
        printf("[MQTT] on_connect: falha (%s)\n", mosquitto_strerror(rc));
    }
    fflush(stdout);
}

int connect_broker(struct mosquitto *mosq, const char *host, int port) 
{
    // Implementar conexão com o broker MQTT
    int rc;

    printf("[MQTT] Conectando ao broker %s:%d...\n", host, port);
    rc = mosquitto_connect(mosq, host, port,  60);

    if (rc == MOSQ_ERR_INVAL)
    {
        printf("[ERRO] mosquitto_connect inválido: %s. Verifique parâmetros.\n", mosquitto_strerror(rc));
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
        vTaskDelete(NULL);
    }
    else {
        printf("[ERRO] mosquitto_connect falhou: %s. Tentando novamente em 2s...\n", mosquitto_strerror(rc));
        fflush(stdout);
    }

    return rc;
}

void vMqttTask(void *pvParameters) {
    printf("[MqttTask] Iniciada.\n");
    fflush(stdout);

    struct mosquitto *mosq = NULL;
    MqttMessage msg;

    const char *token = getenv("TAGO_DEVICE_TOKEN");
    const char *client_id = getenv("TAGO_CLIENT_ID");  // opcional
    const char *freertos_mqtt_host = getenv("freertos_mqtt_host");  // opcional
    const char *freertos_mqtt_port = getenv("freertos_mqtt_port");  // opcional

    if (!token) {
        printf("[ERRO] Variável de ambiente TAGO_DEVICE_TOKEN não definida!\n");
        vTaskDelete(NULL);
    }
    else {
        printf("[MqttTask] Usando TAGO_DEVICE_TOKEN: %s\n", token);
    }

    
    mosquitto_lib_init();
    mosq = mosquitto_new(client_id, true, NULL);
    
    // Nenhum usuário/senha necessário — conexão local
    mosquitto_username_pw_set(mosq, token, "");
    int rc = connect_broker(mosq, freertos_mqtt_host, atoi(freertos_mqtt_port));

    printf("[MQTT] mosquitto_connect iniciado com sucesso.\n");

    fflush(stdout);

    /* Inicia thread interna da libmosquitto para gerenciar I/O e reconexões */
    if (mosquitto_loop_start(mosq) != MOSQ_ERR_SUCCESS) {
        printf("[ERRO] Falha ao iniciar loop do mosquitto.\n");
        mosquitto_disconnect(mosq);
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
        vTaskDelete(NULL);
        return;
    }

    /* Espera até o callback confirmar a conexão */
    // int waitCount = 0;
    // while (!connected && waitCount++ < 100) { // timeout ~100*100ms = 10s
    //     vTaskDelay(pdMS_TO_TICKS(100));
    // }

    // if (!connected) {
    //     printf("[ERRO] Timeout aguardando conexão MQTT. Continuando — loop do mosquitto tentará reconectar.\n");
    // } else {
    //     printf("[MQTT] Conectado ao broker %s:%d\n", TAGO_BROKER, TAGO_PORT);
    // }

    fflush(stdout);

    /* Loop de envio de mensagens */
    while (1) {
        if (xQueueReceive(mqttQueue, &msg, portMAX_DELAY)) {

            int mid = 0;

            rc = mosquitto_publish(mosq, &mid, msg.topic, (int)strlen(msg.payload),
                                   msg.payload, 0, false);

            if(mid == 0) {
                printf("[ERRO] Falha ao obter ID da mensagem publicada em %s.\n", msg.topic);
                fflush(stdout);
            }

            if (rc == MOSQ_ERR_SUCCESS) {
                printf("[OK] Id da mensagem: %d Publicado em %s | Payload: %s\n", mid, msg.topic, msg.payload);
            } else if (rc == MOSQ_ERR_NO_CONN) {
                printf("[ERRO] Sem conexão ao publicar em %s. Aguardando reconexão...\n", msg.topic);
            } else {
                printf("[ERRO] Falha ao publicar em %s: %s\n", msg.topic, mosquitto_strerror(rc));
            }
            fflush(stdout);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    mosquitto_loop_stop(mosq, true);
    mosquitto_disconnect(mosq);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    vTaskDelete(NULL);
}

// ===================== MAIN =====================
int main(void) {
    printf("Iniciando FreeRTOS MQTT TagoIO Simulation...\n");
    fflush(stdout);

    mqttQueue = xQueueCreate(10, sizeof(MqttMessage));
    if (mqttQueue == NULL) {
        printf("[ERRO] Falha ao criar fila MQTT.\n");
        return 1;
    }

    if (xTaskCreate(vSensorTask, "SensorTask", 1024, NULL, 1, NULL) != pdPASS)
        printf("[ERRO] Falha ao criar SensorTask!\n");
    else
        printf("[OK] SensorTask criada.\n");

    if (xTaskCreate(vMqttTask, "MqttTask", 2048, NULL, 1, NULL) != pdPASS)
        printf("[ERRO] Falha ao criar MqttTask!\n");
    else
        printf("[OK] MqttTask criada.\n");

    vTaskStartScheduler();

    // Nunca deve chegar aqui
    for (;;);
    return 0;
}
