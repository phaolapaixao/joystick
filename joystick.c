#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "pico/cyw43_arch.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/netif.h"
#include <math.h>

// Configurações de Wi-Fi
#define WIFI_SSID "ANA"
#define WIFI_PASSWORD "Phaola2023"

// Definição dos pinos do joystick
#define JOYSTICK_X_PIN 26  // ADC0 (GPIO26)
#define JOYSTICK_Y_PIN 27  // ADC1 (GPIO27)
#define JOYSTICK_BUTTON_PIN 22  // GPIO14 para o botão do joystick

// Limiares para determinação das direções
#define DEADZONE 0.2f

// Estrutura para armazenar os dados do joystick
typedef struct {
    float x;
    float y;
    bool button_pressed;
    const char* direction;
    bool changed;  // Flag para indicar se houve mudança
} JoystickData;

// Variável para armazenar o último estado do joystick
static JoystickData last_joystick_state = {0};

// Função para inicializar o ADC para o joystick
void joystick_init() {
    adc_init();
    adc_gpio_init(JOYSTICK_X_PIN);
    adc_gpio_init(JOYSTICK_Y_PIN);
    gpio_init(JOYSTICK_BUTTON_PIN);
    gpio_set_dir(JOYSTICK_BUTTON_PIN, GPIO_IN);
    gpio_pull_up(JOYSTICK_BUTTON_PIN);
}

// Função para verificar se houve mudança no estado do joystick
bool joystick_changed(const JoystickData* new_state) {
    if (fabs(new_state->x - last_joystick_state.x) > 0.1f ||
        fabs(new_state->y - last_joystick_state.y) > 0.1f ||
        new_state->button_pressed != last_joystick_state.button_pressed ||
        strcmp(new_state->direction, last_joystick_state.direction) != 0) {
        return true;
    }
    return false;
}

// Função para ler os valores do joystick
JoystickData read_joystick() {
    JoystickData data;
    
    // Ler valores X e Y (0-4095)
    adc_select_input(0);
    uint16_t x_raw = adc_read();
    adc_select_input(1);
    uint16_t y_raw = adc_read();
    
    // Converter para faixa -1.0 a 1.0 (centro em 0)
    data.x = (x_raw / 2048.0f) - 1.0f;
    data.y = (y_raw / 2048.0f) - 1.0f;
    
    // Inverter eixo Y se necessário
    data.y = -data.y;
    
    // Aplicar deadzone
    float magnitude = sqrtf(data.x * data.x + data.y * data.y);
    if (magnitude < DEADZONE) {
        data.x = 0.0f;
        data.y = 0.0f;
    }
    
    // Ler botão
    data.button_pressed = !gpio_get(JOYSTICK_BUTTON_PIN);
    
    // Determinar direção
    if (magnitude < DEADZONE) {
        data.direction = "Centro";
    } else {
        float angle = atan2f(data.y, data.x);
        
        // Ajustar ângulo para estar entre 0 e 2π
        if (angle < 0) angle += 2 * M_PI;
        
        // Determinar a direção baseada no ângulo
        if (angle < M_PI/8 || angle >= 15*M_PI/8) {
            data.direction = "Norte";
        } else if (angle < 3*M_PI/8) {
            data.direction = "Noroeste";
        } else if (angle < 5*M_PI/8) {
            data.direction = "Oeste";
        } else if (angle < 7*M_PI/8) {
            data.direction = "Sudoeste";
        } else if (angle < 9*M_PI/8) {
            data.direction = "Sul";
        } else if (angle < 11*M_PI/8) {
            data.direction = "Sudeste";
        } else if (angle < 13*M_PI/8) {
            data.direction = "Leste";
        } else {
            data.direction = "Nordeste";
        }
    }
    
    // Verificar se houve mudança
    data.changed = joystick_changed(&data);
    
    // Atualizar o último estado
    if (data.changed) {
        last_joystick_state = data;
    }
    
    return data;
}

// Função para enviar a resposta HTML
void send_html_response(struct tcp_pcb *tpcb) {
    JoystickData joystick = read_joystick();
    
    // Log de acesso
    printf("Dispositivo conectado - Enviando página web...\n");
    if (joystick.changed) {
        printf("Alteração no joystick detectada:\n");
        printf("  Direção: %s\n", joystick.direction);
        printf("  Posição X: %.2f\n", joystick.x);
        printf("  Posição Y: %.2f\n", joystick.y);
        printf("  Botão: %s\n", joystick.button_pressed ? "Pressionado" : "Liberado");
    }
    
    char html[2048];
    snprintf(html, sizeof(html),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "\r\n"
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "<title>Representacao da Posicao de um Farol Maritimo</title>\n"
        "<meta http-equiv=\"refresh\" content=\"0.5\">\n"
        "</head>\n"
        "<body>\n"
        "<h1>Representacao da Posicao de um Farol Maritimo</h1>\n"
        "<p>Direcao: %s</p>\n"
        "<p>Posicao X: %.2f</p>\n"
        "<p>Posicao Y: %.2f</p>\n"
        "<p>Botao: %s</p>\n"
        "</body>\n"
        "</html>\n",
        joystick.direction,
        joystick.x,
        joystick.y,
        joystick.button_pressed ? "Pressionado" : "Liberado"
    );

    tcp_write(tpcb, html, strlen(html), TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);
}

// Função de callback para processar requisições HTTP
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (!p) {
        tcp_close(tpcb);
        tcp_recv(tpcb, NULL);
        return ERR_OK;
    }

    // Log da requisição recebida
    printf("Requisição HTTP recebida\n");
    
    pbuf_free(p);
    
    // Envia a resposta HTML
    send_html_response(tpcb);
    return ERR_OK;
}

// Função de callback ao aceitar conexões TCP
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err) {
    // Log de nova conexão
    printf("Nova conexão TCP estabelecida\n");
    tcp_recv(newpcb, tcp_server_recv);
    return ERR_OK;
}

// Função principal
int main() {
    stdio_init_all();
    
    // Inicializar joystick
    joystick_init();
    
    // Inicializa Wi-Fi
    while (cyw43_arch_init()) {
        printf("Falha ao inicializar Wi-Fi\n");
        sleep_ms(1000);
    }

    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
    cyw43_arch_enable_sta_mode();

    printf("Conectando ao Wi-Fi...\n");
    while (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 20000)) {
        printf("Falha ao conectar ao Wi-Fi\n");
        sleep_ms(1000);
    }

    printf("Conectado ao Wi-Fi\n");
    if (netif_default) {
        printf("IP do dispositivo: %s\n", ipaddr_ntoa(&netif_default->ip_addr));
    }

    // Configura o servidor TCP
    struct tcp_pcb *server = tcp_new();
    if (!server) {
        printf("Falha ao criar servidor TCP\n");
        return -1;
    }

    if (tcp_bind(server, IP_ADDR_ANY, 80) != ERR_OK) {
        printf("Falha ao associar servidor TCP à porta 80\n");
        return -1;
    }

    server = tcp_listen(server);
    tcp_accept(server, tcp_server_accept);

    printf("Servidor ouvindo na porta 80\n");

    // Loop principal
    while (true) {
        cyw43_arch_poll();
        
        // Verificar periodicamente o estado do joystick
        static absolute_time_t last_check = 0;
        if (absolute_time_diff_us(last_check, get_absolute_time()) > 100000) { // 100ms
            JoystickData current = read_joystick();
            if (current.changed) {
                printf("Joystick alterado (não solicitado):\n");
                printf("  Direcao: %s\n", current.direction);
                printf("  Posicao X: %.2f\n", current.x);
                printf("  Posicao Y: %.2f\n", current.y);
                printf("  Botao: %s\n", current.button_pressed ? "Pressionado" : "Liberado");
            }
            last_check = get_absolute_time();
        }
        
        sleep_ms(10);
    }

    cyw43_arch_deinit();
    return 0;
}