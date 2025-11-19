/*
  Sistema: AirWatch - Supervisor de Ambiente Sem Fio (Simulação Wokwi)
  

  Componentes:
  - FreeRTOS (ESP32)
  - Três processos assíncronos com prioridades distintas
  - Mutex para acesso a tabela de permissões
  - Fila de eventos de anomalia
  - WDT (nova API do ESP-IDF)
*/

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_task_wdt.h"
#include <vector>

// ---------------------------------------------------
// DEFINIÇÕES
// ---------------------------------------------------

#define LED_PIN 2

static const char* FAKE_WIFI_POOL[] = {
    "Net-Office-1",
    "GuestLobby",
    "ProdCluster24",
    "Unknown_Station",
    "VoidBeacon_77"
};

// ---------------------------------------------------
// TIPOS
// ---------------------------------------------------

struct NetIncident {
    String ssid;
    uint32_t ts;
};

// ---------------------------------------------------
// OBJETOS RTOS
// ---------------------------------------------------

static SemaphoreHandle_t lockAllowlist;
static QueueHandle_t incidentQueue;
static std::vector<String>* allowlistRef = nullptr;

// ---------------------------------------------------
// FUNÇÕES INTERNAS
// ---------------------------------------------------

// Retorna SSID aleatório (simulação)
static String acquireRandomSSID() {
    return FAKE_WIFI_POOL[random(0, 5)];
}

// Consulta se SSID está na lista de permitido
static bool ssidAllowed(const String& s) {
    bool allowed = false;

    if (xSemaphoreTake(lockAllowlist, pdMS_TO_TICKS(150)) == pdTRUE) {
        for (const auto& entry : *allowlistRef) {
            if (entry.equals(s)) {
                allowed = true;
                break;
            }
        }
        xSemaphoreGive(lockAllowlist);
    }

    return allowed;
}

// ---------------------------------------------------
// TASK: RedeScanner (prioridade intermediária)
// ---------------------------------------------------

static void RedeScanner(void* args) {
    esp_task_wdt_add(nullptr);

    while (true) {
        esp_task_wdt_reset();

        String current = acquireRandomSSID();
        Serial.printf("[SCAN] Capturado: %s\n", current.c_str());

        if (!ssidAllowed(current)) {
            Serial.println("[SCAN] Evento suspeito: origem não reconhecida.");

            NetIncident inc;
            inc.ssid = current;
            inc.ts = millis();

            xQueueSend(incidentQueue, &inc, pdMS_TO_TICKS(80));
        }

        vTaskDelay(pdMS_TO_TICKS(1800));
    }
}

// ---------------------------------------------------
// TASK: ConsoleReporter (prioridade baixa)
// ---------------------------------------------------

static void ConsoleReporter(void* args) {
    esp_task_wdt_add(nullptr);

    NetIncident inc;

    while (true) {
        esp_task_wdt_reset();

        if (xQueueReceive(incidentQueue, &inc, portMAX_DELAY)) {
            Serial.println("=== REGISTRO DE OCORRÊNCIA ===");
            Serial.printf("Origem: %s\n", inc.ssid.c_str());
            Serial.printf("Marca temporal: %u ms\n", inc.ts);
            Serial.println("================================");

            // Piscar LED de alerta
            for (int k = 0; k < 3; k++) {
                digitalWrite(LED_PIN, HIGH);
                vTaskDelay(pdMS_TO_TICKS(160));
                digitalWrite(LED_PIN, LOW);
                vTaskDelay(pdMS_TO_TICKS(160));
            }
        }
    }
}

// ---------------------------------------------------
// TASK: TickRoutine (prioridade alta)
// ---------------------------------------------------

static void TickRoutine(void* args) {
    esp_task_wdt_add(nullptr);

    while (true) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(800));  // rotina dummy
    }
}

// ---------------------------------------------------
// SETUP
// ---------------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(400);

    pinMode(LED_PIN, OUTPUT);

    lockAllowlist = xSemaphoreCreateMutex();
    incidentQueue = xQueueCreate(12, sizeof(NetIncident));

    allowlistRef = new std::vector<String>();
    allowlistRef->push_back("Net-Office-1");
    allowlistRef->push_back("ProdCluster24");
    allowlistRef->push_back("CoreNetwork-A");
    allowlistRef->push_back("InternalOps5G");

    Serial.println("[BOOT] Allowlist carregada.");

    // Inicialização do watchdog
    static esp_task_wdt_config_t wcfg = {
        .timeout_ms = 10000,
        .trigger_panic = true
    };

    esp_task_wdt_init(&wcfg);
    Serial.println("[BOOT] Watchdog pronto.");

    // Criação das tasks principais
    xTaskCreatePinnedToCore(TickRoutine,     "Tick",     4096, nullptr, 3, nullptr, 1);
    xTaskCreatePinnedToCore(RedeScanner,     "RScanner", 4096, nullptr, 2, nullptr, 1);
    xTaskCreatePinnedToCore(ConsoleReporter, "Reporter", 4096, nullptr, 1, nullptr, 1);

    Serial.println("[BOOT] Processos agendados.");
}

// ---------------------------------------------------
// LOOP PRINCIPAL (ocioso)
// ---------------------------------------------------

void loop() {
    vTaskDelay(pdMS_TO_TICKS(900));
}
