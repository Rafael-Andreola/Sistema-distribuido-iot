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
#define TAGO_BROKER "mqtt.tago.io"
#define TAGO_PORT 1883  // Porta segura com TLS

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

        snprintf(msg.topic, sizeof(msg.topic), "tago/data");
        snprintf(msg.payload, sizeof(msg.payload),
                 "{\"variable\":\"temperature\",\"value\":%.2f}", value);

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

void vMqttTask(void *pvParameters) {
    printf("[MqttTask] Iniciada.\n");
    fflush(stdout);

    struct mosquitto *mosq = NULL;
    MqttMessage msg;

    const char *token = getenv("TAGO_DEVICE_TOKEN");
    const char *client_id = getenv("TAGO_CLIENT_ID");  // opcional

    if (!token) {
        printf("[ERRO] Variável de ambiente TAGO_DEVICE_TOKEN não definida!\n");
        vTaskDelete(NULL);
        return;
    }

    mosquitto_lib_init();

    mosq = mosquitto_new(client_id ? client_id : "freertos_tago_client", true, NULL);
    if (!mosq) {
        printf("[ERRO] Falha ao criar cliente MQTT.\n");
        mosquitto_lib_cleanup();
        vTaskDelete(NULL);
        return;
    }

    mosquitto_username_pw_set(mosq, "token" , token);

    // if (mosquitto_tls_set(mosq, NULL, "/etc/ssl/certs", NULL, NULL, NULL) != MOSQ_ERR_SUCCESS) {
    //     printf("[ERRO] Falha ao configurar TLS.\n");
    //     mosquitto_destroy(mosq);
    //     mosquitto_lib_cleanup();
    //     vTaskDelete(NULL);
    //     return;
    // }

    // mosquitto_tls_opts_set(mosq, 1, "tlsv1.2", NULL);

    /* Setup callback e flag de conexão */
    // int connected = 0;
    // mosquitto_user_data_set(mosq, &connected);
    // mosquitto_connect_callback_set(mosq, on_connect);

    /* Tentar connect_async repetidamente (evita EINTR em connect bloqueante) */
    int rc;
    do {
        rc = mosquitto_connect(mosq, TAGO_BROKER, TAGO_PORT, 0);

        if (rc == MOSQ_ERR_SUCCESS) {
            printf("[MQTT] mosquitto_connect iniciado com sucesso.\n");
        }
        else {
            printf("[ERRO] mosquitto_connect falhou: %s. Tentando novamente em 2s...\n",
                   mosquitto_strerror(rc));

            fflush(stdout);
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    } while (rc != MOSQ_ERR_SUCCESS);

    printf("[MQTT] Conectando ao broker %s:%d...\n", TAGO_BROKER, TAGO_PORT);
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
