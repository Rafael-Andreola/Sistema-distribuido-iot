# FreeRTOS MQTT TagoIO Simulation

Simulação de dispositivos FreeRTOS que publicam telemetria MQTT (para ThingsBoard/TagoIO). O projeto contém um binário C (FreeRTOS POSIX) que publica mensagens via libmosquitto.

## Estrutura do repositório

- `.docker/`
  - `docker-compose.yml` - compose para levantar Postgres, ThingsBoard e 4 dispositivos (device_1..4).
  - `Dockerfile` - Dockerfile usado pelo `docker-compose` para construir a imagem do dispositivo.
- `src/`
  - `CMakeLists.txt` - CMake do projeto
  - `main.c` - código principal (tarefas, MQTT, FreeRTOS POSIX glue).
  - `FreeRTOSConfig.h`
- `.gitignore` - ignora `.env`.

## Requisitos para build local (fora do Docker)

- cmake >= 3.13
- gcc / build-essential
- libmosquitto-dev (para link com mosquitto)
- pthreads (normalmente disponível no Linux)

Build manual (na raiz do projeto):

```powershell
mkdir build; cmake -S src -B build; cmake --build build --target freertos_mqtt_tago
```

## Build & execução com Docker (recomendado)

A forma suportada nesta árvore é usar `docker-compose` a partir da raiz do projeto que referencia o contexto correto:

```powershell
# a partir da raiz do repositório
docker compose -f .\.docker\docker-compose.yml up --build -d
```

Se você executar `docker build` dentro de `.docker` sem ajustar o contexto, comandos `COPY src` (ou `COPY ../src`) falharão porque Docker não permite acessar arquivos fora do contexto de build.

## Variáveis de ambiente (definidas no compose por dispositivo)

- `CLIENT_ID` - ID do cliente MQTT
- `DEVICE_TOKEN` - token do dispositivo (usado como username no mosquitto)
- `MQTT_HOST` - host do broker (ex.: `host.docker.internal`)
- `MQTT_PORT` - porta MQTT (ex.: `1883`)
- `QUEUE_SIZE` - tamanho da fila interna (xQueueCreate)
- `SENSOR_INTERVAL_MS` - intervalo de leitura do sensor (ms)
- `ENABLE_INFOS` - 0/1, ativa mensagens `print_info` (logs informativos)

Exemplo (já presente no `docker-compose.yml`):

```yaml
environment:
  CLIENT_ID: dev001
  DEVICE_TOKEN: T1_TOKEN
  MQTT_HOST: host.docker.internal
  MQTT_PORT: 1883
  QUEUE_SIZE: 10
  SENSOR_INTERVAL_MS: 5000
  ENABLE_INFOS: 0
```

## Erros comuns e troubleshooting

- `failed to calculate checksum of ref ... "/src": not found`:
  - Causa: Dockerfile usa `COPY ../src` mas o comando `docker build` foi executado com um contexto que não inclui esse caminho. Solução:
    - Use `docker-compose` com `context: ..` (recomendado). Ou
    - Rode `docker build -f ./.docker/Dockerfile .` a partir da raiz do repo (context `.`) — não rode `docker build` dentro de `.docker` sem ajustar o contexto.

- `too many arguments to function 'print_info'`:
  - Causa: função `print_info` originalmente não era variádica mas era chamada como `printf(format, ...)`.
  - Solução: função foi alterada para aceitar `const char *format, ...` e usa `vprintf`.

- Cabeçalhos não encontrados durante análise estática (ex.: `mosquitto.h`, `FreeRTOS.h`):
  - No ambiente de desenvolvimento local, você precisa instalar as dependências (`libmosquitto-dev`) ou apontar corretamente os include paths. No Dockerfile as dependências de build são instaladas (cmake, libmosquitto-dev, etc.), então a compilação dentro do container encontra os headers.

## Como habilitar logs informativos

Defina `ENABLE_INFOS` como `1` no `docker-compose.yml` (ou variável de ambiente no `docker run`) para ativar chamadas a `print_info(...)`.

## Comandos úteis (PowerShell)

```powershell
# Build & up usando docker-compose (recomendado)
docker compose -f .\.docker\docker-compose.yml up --build -d

# Build direto (contexto = raiz do repo)
docker build -t freertos_mqtt_tago -f .\.docker\Dockerfile .
```

## Notas finais

- A configuração atual assume que o `docker-compose.yml` está em `.docker/` e que seu `build.context` é `..` (raiz do projeto). Isso permite que o `Dockerfile` em `.docker/` utilize `COPY src` e `COPY CMakeLists.txt` sem tentar acessar arquivos fora do contexto.
- Se reorganizar a árvore, sempre ajuste o `build.context` no `docker-compose.yml` ou o `docker build` para apontar para o diretório que contém os arquivos que o Dockerfile precisa copiar.

---
