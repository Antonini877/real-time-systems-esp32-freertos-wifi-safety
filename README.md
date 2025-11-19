# AirWatch – Supervisor de Ambiente Sem Fio (ESP32 + FreeRTOS)

Sistema desenvolvido e simulado no Wokwi que demonstra um caso de uso de tempo real com FreeRTOS: monitorar continuamente a rede Wi‑Fi “percebida” pelo dispositivo, conferir se ela pertence a uma lista de redes seguras (allowlist) e, ao detectar conexão/ambiente não autorizado, emitir alerta imediato (log e sinalização por LED).

## Objetivo do Case
- Monitorar, em tempo real, o SSID corrente “observado” pelo dispositivo.
- Validar o SSID contra uma lista de redes seguras.
- Gerar um alerta imediato quando a rede não estiver autorizada.
- Isolar responsabilidades em tarefas FreeRTOS com prioridades distintas para garantir responsividade e previsibilidade.

## Como a solução atende ao requisito
- A tarefa `RedeScanner` captura periodicamente um SSID (simulado) e verifica, com acesso protegido por mutex, se está na allowlist. Quando encontra uma rede não permitida, publica um evento em `incidentQueue`.
- A tarefa `ConsoleReporter` consome os eventos da fila e emite um alerta imediato (mensagens no serial e piscando o LED).
- A tarefa `TickRoutine` (maior prioridade) mantém o watchdog saudável e contribui para o ritmo do sistema, garantindo que tarefas críticas não percam seus prazos.

## Conceitos FreeRTOS aplicados
- Tarefas (Tasks): três tarefas independentes, cada uma com função clara:
  - `TickRoutine` (prioridade 3) – mantém o sistema ativo e o watchdog resetado.
  - `RedeScanner` (prioridade 2) – captura SSID atual e valida contra a allowlist.
  - `ConsoleReporter` (prioridade 1) – trata incidentes e alerta usuário.
- Prioridades: definidas para garantir que o watchdog e a responsividade do scanner não sejam atrasados por operações de saída (I/O) do reporter.
- Mutex (`lockAllowlist`): protege acesso concorrente à allowlist, garantindo integridade de leitura/escrita.
- Fila (`incidentQueue`): desacopla detecção (produtor) e notificação (consumidor), evitando bloqueios e permitindo tratamento ordenado de eventos.
- Watchdog (`esp_task_wdt_*`): previne travamentos e reforça disciplina de tempo de execução; cada tarefa registra e reseta periodicamente.
- Pinagem em core: as três tarefas são “pinned” no mesmo core (1) para reduzir migração entre cores e manter previsibilidade da latência em ambiente simples de simulação.

## Arquitetura e Fluxo
- Scanner captura SSID → consulta allowlist (prot. por mutex) → se não permitido, cria `NetIncident` e envia para fila.
- Reporter bloqueia na fila (consumo) → ao receber, loga dados e pisca LED.
- TickRoutine roda com período mais curto para manter WDT e ritmo geral.

### Diagrama (ASCII)
```
+------------------------------ Core 1 ------------------------------+
|                                                                    |
|  [TickRoutine prio=3] ---> (Watchdog Reset)                        |
|                                                                    |
|  [RedeScanner prio=2] --uses--> [lockAllowlist] --reads--> [Allowlist]
|            |                                                       |
|            +-- if SSID not allowed --> (NetIncident) --> [incidentQueue]
|                                                                    |
|  [ConsoleReporter prio=1] <--consumes-- [incidentQueue]            |
|            |                                                       |
|            +-- logs --> (Serial)                                   |
|            +-- alerts --> (LED blink)                              |
|                                                                    |
+--------------------------------------------------------------------+

Shared resources:
- [lockAllowlist] Mutex protecting access to allowlist (vector<String>).
- [incidentQueue] Queue<size=12> carrying NetIncident { ssid, ts }.
- Allowlist: {"Net-Office-1","ProdCluster24","CoreNetwork-A","InternalOps5G"}

Timing:
- TickRoutine: delay 800 ms
- RedeScanner: delay 1800 ms
- Reporter: blocks on queue (portMAX_DELAY)

Watchdog:
- timeout 10 s; tasks add + reset periodically
```

## Principais componentes e APIs utilizadas
- `xTaskCreatePinnedToCore` – criação de tarefas com prioridade e pinagem de core.
- `xSemaphoreCreateMutex` e `xSemaphoreTake/Give` – proteção de acesso à allowlist.
- `xQueueCreate`, `xQueueSend`, `xQueueReceive` – comunicação assíncrona entre tarefas.
- `esp_task_wdt_init`, `esp_task_wdt_add`, `esp_task_wdt_reset` – watchdog de tarefas.
- `vTaskDelay(pdMS_TO_TICKS(n))` – temporização cooperativa sem bloqueio.
- `Serial.printf/println` e `digitalWrite` – notificação e alerta visual.

## Estrutura de arquivos
- `src/main.ino` – implementação das tarefas FreeRTOS, allowlist, fila e alerta.
- `src/diagram.json` – diagrama Wokwi (placa ESP32 + conexão ao serial).
- `img/` – screenshots do ambiente de simulação.

## Execução no Wokwi
1. Abra o projeto no Wokwi.
2. Garanta que `diagram.json` contenha a placa `board-esp32-devkit-c-v4` e conexões de serial.
3. Execute a simulação. O console mostrará entradas como:
   - `[SCAN] Capturado: <SSID>`
   - `[SCAN] Evento suspeito: origem não reconhecida.` (quando não autorizado)
   - Em seguida, o bloco “REGISTRO DE OCORRÊNCIA” e o LED piscando.

## Evidências de Funcionamento (Screenshots)
As imagens abaixo mostram a simulação em Wokwi com logs e alerta visual:

![Simulação Wokwi – logs e alerta](img/Screenshot%202025-11-19%20195528.png)
*Console com registros de ocorrências detectadas e sequência de alerta.*

![Simulação Wokwi – fluxo de incidentes](img/Screenshot%202025-11-19%20195622.png)
*Visão complementar evidenciando o funcionamento do fluxo de incidentes e alerta.*

## Parâmetros de tempo e responsividade
- `TickRoutine` → `vTaskDelay(800ms)`; mantém WDT fresco e ritmo do sistema.
- `RedeScanner` → `vTaskDelay(1800ms)`; “janela” de amostragem da rede simulada.
- `ConsoleReporter` → bloqueia em `xQueueReceive(portMAX_DELAY)`; responde assim que há evento.
- Watchdog: timeout de `10s`, configurado via `esp_task_wdt_init`.
- Fila: profundidade `12` eventos (`xQueueCreate(12, sizeof(NetIncident))`).

## Adaptação para hardware real (ESP32)
- Substitua a função de simulação `acquireRandomSSID()` por leitura do SSID real:
  - API Arduino: `WiFi.begin(...); WiFi.SSID();` ou `WiFi.scanNetworks()` e compare com a rede associada.
  - ESP-IDF: use eventos `SYSTEM_EVENT_STA_CONNECTED`, `SYSTEM_EVENT_STA_GOT_IP` e obtenha SSID via `wifi_config_t` ou `esp_wifi_sta_get_ap_info`.
- Mantenha o mesmo desenho RTOS: Scanner captura estado da associação, valida na allowlist (mutex), e publica eventos na fila para o Reporter emitir alerta.
- Ajuste períodos de amostragem e limites do WDT às necessidades de latência do produto.

## Observações de segurança e governança
- Allowlist inicial é carregada no `setup()` e protegida por mutex; em produção, pode ser atualizada por OTA/flash seguro.
- O alerta por LED é apenas indicação visual; em dispositivos finais, inclua também telemetria, syslog, ou ação de mitigação (desconectar, bloquear tráfego).
- Separe domínios de tempo real (detecção) e de I/O (report) para manter a responsividade.

## Referências de código
- Tarefas: `TickRoutine`, `RedeScanner`, `ConsoleReporter`.
- Recursos RTOS: `lockAllowlist` (mutex), `incidentQueue` (fila), `allowlistRef` (coleção protegida).
- Estruturas: `NetIncident` carrega SSID e timestamp (`millis`).

---
Este protótipo ilustra um padrão robusto para monitoramento de rede com FreeRTOS: tarefas bem definidas, comunicação por fila, acesso protegido a dados compartilhados e watchdog para garantir confiabilidade, atendendo ao requisito de alerta imediato em caso de rede não autorizada.