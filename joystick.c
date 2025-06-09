#include "pico/stdlib.h"
#include "hardware/adc.h" 
#include "pico/cyw43_arch.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/dns.h"
#include "lwip/altcp.h"
#include "lwip/altcp_tcp.h"
#include <math.h>

// Configurações
#define WIFI_SSID "ANA"
#define WIFI_PASSWORD "Phaola2023"
#define SERVER_IP "192.168.18.219"
#define SERVER_PORT 5000
#define MAX_RETRIES 3
#define RETRY_DELAY_MS 2000
#define SEND_INTERVAL_MS 100
#define CONNECTION_TIMEOUT_MS 5000

// Pinos
#define JOYSTICK_X_PIN 26
#define JOYSTICK_Y_PIN 27
#define JOYSTICK_BUTTON_PIN 22

// Limiares
#define DEADZONE 0.2f
#define CHANGE_THRESHOLD 0.1f

typedef struct {
    float x;
    float y;
    bool button_pressed;
    const char* direction;
    bool changed;
} JoystickData;

static JoystickData last_sent_state = {0};
static int connection_retries = 0;

void joystick_init() {
    adc_init();
    adc_gpio_init(JOYSTICK_X_PIN);
    adc_gpio_init(JOYSTICK_Y_PIN);
    gpio_init(JOYSTICK_BUTTON_PIN);
    gpio_set_dir(JOYSTICK_BUTTON_PIN, GPIO_IN);
    gpio_pull_up(JOYSTICK_BUTTON_PIN);
}

bool joystick_changed(const JoystickData* new_state) {
    if (fabs(new_state->x - last_sent_state.x) > CHANGE_THRESHOLD ||
        fabs(new_state->y - last_sent_state.y) > CHANGE_THRESHOLD ||
        new_state->button_pressed != last_sent_state.button_pressed ||
        strcmp(new_state->direction, last_sent_state.direction) != 0) {
        return true;
    }
    return false;
}

JoystickData read_joystick() {
    JoystickData data;
    
    // Ler valores analógicos
    adc_select_input(0);
    uint16_t x_raw = adc_read();
    adc_select_input(1);
    uint16_t y_raw = adc_read();
    
    // Normalizar valores (-1.0 a 1.0)
    data.x = (x_raw / 2048.0f) - 1.0f;
    data.y = (y_raw / 2048.0f) - 1.0f;
    data.y = -data.y;  // Inverter eixo Y se necessário
    
    // Aplicar deadzone
    float magnitude = sqrtf(data.x * data.x + data.y * data.y);
    if (magnitude < DEADZONE) {
        data.x = 0.0f;
        data.y = 0.0f;
        magnitude = 0.0f;
    }
    
    // Ler botão
    data.button_pressed = !gpio_get(JOYSTICK_BUTTON_PIN);
    
    // Determinar direção
    if (magnitude < DEADZONE) {
        data.direction = "Centro";
    } else {
        float angle = atan2f(data.y, data.x) * (180.0f / M_PI);
        if (angle < 0) angle += 360.0f;
        
        if (angle >= 337.5f || angle < 22.5f) {
            data.direction = "Leste";
        } else if (angle < 67.5f) {
            data.direction = "Nordeste";
        } else if (angle < 112.5f) {
            data.direction = "Norte";
        } else if (angle < 157.5f) {
            data.direction = "Noroeste";
        } else if (angle < 202.5f) {
            data.direction = "Oeste";
        } else if (angle < 247.5f) {
            data.direction = "Sudoeste";
        } else if (angle < 292.5f) {
            data.direction = "Sul";
        } else {
            data.direction = "Sudeste";
        }
    }
    
    data.changed = joystick_changed(&data);
    
    return data;
}

err_t send_joystick_data(const JoystickData *data) {
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) {
        printf("Erro: Falha ao criar PCB\n");
        return ERR_MEM;
    }

    ip_addr_t server_addr;
    if (!ipaddr_aton(SERVER_IP, &server_addr)) {
        printf("Erro: Endereço IP inválido\n");
        tcp_close(pcb);
        return ERR_ARG;
    }

    // Configurar timeout
    tcp_nagle_disable(pcb); 
    
    // Conectar
    err_t err = tcp_connect(pcb, &server_addr, SERVER_PORT, NULL);
    if (err != ERR_OK) {
        printf("Erro ao conectar: %d\n", err);
        tcp_close(pcb);
        return err;
    }

    // Criar payload JSON
    char json_data[128];
    snprintf(json_data, sizeof(json_data),
        "{\"x\":%.2f,\"y\":%.2f,\"button\":%s,\"direction\":\"%s\"}",
        data->x, data->y,
        data->button_pressed ? "true" : "false",
        data->direction);

    // Criar requisição HTTP completa
    char request[512];
    int request_len = snprintf(request, sizeof(request),
        "POST /api/joystick HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "User-Agent: PicoW/1.0\r\n"
        "Accept: */*\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        SERVER_IP, SERVER_PORT,
        (int)strlen(json_data),
        json_data);

    // Enviar dados
    err = tcp_write(pcb, request, request_len, TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        printf("Erro ao escrever dados: %d\n", err);
        tcp_close(pcb);
        return err;
    }

    err = tcp_output(pcb);
    if (err != ERR_OK) {
        printf("Erro ao enviar dados: %d\n", err);
        tcp_close(pcb);
        return err;
    }

    // Esperar um pouco para garantir o envio
    sleep_ms(100);
    tcp_close(pcb);
    return ERR_OK;
}

bool try_wifi_connection() {
    printf("Tentando conectar ao Wi-Fi...\n");
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 10000)) {
        printf("Falha ao conectar ao Wi-Fi\n");
        return false;
    }
    printf("Conectado ao Wi-Fi\n");
    if (netif_default) {
        printf("IP: %s\n", ipaddr_ntoa(&netif_default->ip_addr));
    }
    return true;
}

int main() {
    stdio_init_all();
    joystick_init();
    
    if (cyw43_arch_init()) {
        printf("Falha ao inicializar Wi-Fi\n");
        return -1;
    }

    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
    cyw43_arch_enable_sta_mode();

    // Tentar conectar ao Wi-Fi
    while (!try_wifi_connection()) {
        sleep_ms(RETRY_DELAY_MS);
    }

    absolute_time_t last_send_time = get_absolute_time();
    
    while (true) {
        JoystickData current = read_joystick();
        
        if (current.changed && absolute_time_diff_us(last_send_time, get_absolute_time()) >= SEND_INTERVAL_MS * 1000) {
            printf("Enviando: x=%.2f, y=%.2f, button=%d, direction=%s\n",
                  current.x, current.y, current.button_pressed, current.direction);
            
            err_t err = send_joystick_data(&current);
            if (err == ERR_OK) {
                last_sent_state = current;
                connection_retries = 0;
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0); // LED aceso = sucesso
            } else {
                printf("Erro ao enviar (tentativa %d/%d)\n", connection_retries + 1, MAX_RETRIES);
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1); // LED apagado = erro
                if (++connection_retries >= MAX_RETRIES) {
                    printf("Reconectando ao Wi-Fi...\n");
                    try_wifi_connection();
                    connection_retries = 0;
                }
            }
            
            last_send_time = get_absolute_time();
        }
        
        sleep_ms(10);
    }

    cyw43_arch_deinit();
    return 0;
}