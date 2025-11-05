# ============================
# Etapa 1 - Compilação (builder)
# ============================
FROM debian:bullseye-slim AS builder

# Instala dependências necessárias para compilar o projeto
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libssl-dev \
    libmosquitto-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Diretório de trabalho
WORKDIR /app

# Clona o FreeRTOS Kernel (somente o necessário)
RUN git clone --depth 1 https://github.com/FreeRTOS/FreeRTOS-Kernel.git

# Copia o código-fonte do projeto
COPY src .

# Compila o projeto
RUN mkdir -p build && \
    cmake -S . -B build && \
    cmake --build build --target freertos_mqtt_tago

# ============================
# Etapa 2 - Execução (runtime)
# ============================
FROM debian:bullseye-slim AS runtime

# Instala apenas o que é necessário para rodar
RUN apt-get update && apt-get install -y \
    libmosquitto1 \
    ca-certificates \
    iputils-ping \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copia o executável da etapa anterior
COPY --from=builder /app/build/freertos_mqtt_tago /app/freertos_mqtt_tago

# Permissão de execução
RUN chmod +x /app/freertos_mqtt_tago

# Comando padrão de execução
CMD ["./freertos_mqtt_tago"]
