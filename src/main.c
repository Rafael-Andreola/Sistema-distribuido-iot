#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <mosquitto.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

QueueHandle_t queue;
int enable_infos = 0;

// Estrutura de mensagem
typedef struct {
    char topic[64];
    char payload[128];
} MqttMessage;

void print_info(const char *format, ...) {
    if (enable_infos) {
        va_list args;
        va_start(args, format);
        printf("[INFO] ");

        vprintf(format, args);
        
        va_end(args);
        fflush(stdout);
    }
}

// Função simulada de leitura do sensor
float read_sensor() {
    return ((rand() % 9000) / 100.0f) - 20;  // Gera valor entre -20.0 e 70.0
}

int is_outlier(float value) {
    return value < -10.0f;
}

// ===================== TAREFA SENSOR =====================
void vSensorTask(void *pvParameters) {
    print_info("[SensorTask] Iniciada.\n");

    const char *sensor_interval = getenv("SENSOR_INTERVAL_MS");

    while (1) {
        float value = read_sensor();

        if(is_outlier(value) == 0)
        {
            MqttMessage msg;
    
            snprintf(msg.topic, sizeof(msg.topic), "v1/devices/me/telemetry");
            snprintf(msg.payload, sizeof(msg.payload),
                     "{\"temperature\": %.2f}", value);
    
            if (xQueueSend(queue, &msg, portMAX_DELAY) == pdPASS) {
                print_info("[SensorTask] Valor lido: %.2f\n", value);
            } else {
                printf("[SensorTask] Erro ao enviar para fila MQTT!\n");
                fflush(stdout);
            }
        }
        else {
            print_info("[SensorTask] Valor lido: %.2f é um outlier. Ignorando...\n", value);
        }

        vTaskDelay(pdMS_TO_TICKS(sensor_interval ? atoi(sensor_interval) : 2000));
    }
}

int connect_broker(struct mosquitto *mosq, const char *host, int port) 
{
    print_info("[MQTT] Conectando ao broker %s:%d...\n", host, port);

    int rc;

    while (rc != MOSQ_ERR_SUCCESS)
    {
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

        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    return rc;
}

void sendMessages(struct mosquitto *mosq) {
    MqttMessage msg;
    
    const char *send_interval = getenv("SEND_INTERVAL_MS");

    while (1) {
        if (xQueueReceive(queue, &msg, portMAX_DELAY)) {
            int mid = 0;

            int rc = mosquitto_publish(mosq, &mid, msg.topic, (int)strlen(msg.payload),
                                   msg.payload, 1, false);

            if(mid == 0) {
                printf("[ERRO] Falha ao obter ID da mensagem publicada em %s.\n", msg.topic);
            }

            if (rc == MOSQ_ERR_SUCCESS) {
                print_info("[OK] Id da mensagem: %d Publicado em %s | Payload: %s\n", mid, msg.topic, msg.payload);
            } else if (rc == MOSQ_ERR_NO_CONN) {
                printf("[ERRO] Sem conexão ao publicar em %s. Aguardando reconexão...\n", msg.topic);
            } else {
                printf("[ERRO] Falha ao publicar em %s: %s\n", msg.topic, mosquitto_strerror(rc));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(atoi(send_interval)));
    }
}

void vMqttTask(void *pvParameters) {
    print_info("[MqttTask] Iniciada.\n");

    struct mosquitto *mosq = NULL;

    const char *token = getenv("DEVICE_TOKEN");
    const char *client_id = getenv("CLIENT_ID");
    const char *mqtt_host = getenv("MQTT_HOST");
    const char *mqtt_port = getenv("MQTT_PORT");

    if (!token) {
        printf("[ERRO] Variável de ambiente DEVICE_TOKEN não definida!\n");
        vTaskDelete(NULL);
    }

    print_info("[MqttTask] Usando DEVICE_TOKEN: %s\n", token);

    mosquitto_lib_init();
    mosq = mosquitto_new(client_id, true, NULL);

    if (!mosq) {
        printf("[ERRO] Falha ao criar instância do mosquitto.\n");
        mosquitto_lib_cleanup();
        vTaskDelete(NULL);
    }

    mosquitto_username_pw_set(mosq, token, "");
    int rc = connect_broker(mosq, mqtt_host, atoi(mqtt_port));

    print_info("[MQTT] mosquitto_connect iniciado com sucesso.\n");

    /* Inicia thread interna da libmosquitto para gerenciar I/O e reconexões */
    if (mosquitto_loop_start(mosq) != MOSQ_ERR_SUCCESS) {
        printf("[ERRO] Falha ao iniciar loop do mosquitto.\n");
        mosquitto_disconnect(mosq);
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
        vTaskDelete(NULL);
        return;
    }

    sendMessages(mosq);

    mosquitto_loop_stop(mosq, true);
    mosquitto_disconnect(mosq);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    vTaskDelete(NULL);
}

void create_queue() {

    const char *queue_size = getenv("QUEUE_SIZE");

    queue = xQueueCreate(atoi(queue_size), sizeof(MqttMessage));

    if (queue == NULL) {
        printf("[ERRO] Falha ao criar fila.\n");
        exit(1);
    }
}

// ===================== MAIN =====================
int main(void) {

    enable_infos = atoi(getenv("ENABLE_INFOS"));

    print_info("Iniciando FreeRTOS Simulation...\n");

    create_queue();

    if (xTaskCreate(vSensorTask, "SensorTask", 1024, NULL, 1, NULL) == pdPASS)
        print_info("[OK] SensorTask criada.\n");
    else
    {
        printf("[ERRO] Falha ao criar SensorTask!\n");
        return 1;
    }

    if (xTaskCreate(vMqttTask, "MqttTask", 2048, NULL, 1, NULL) == pdPASS)
        print_info("[OK] MqttTask criada.\n");
    else
    {
        printf("[ERRO] Falha ao criar MqttTask!\n");
        return 1;
    }

    vTaskStartScheduler();

    return 0;
}
