#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "pico/cyw43_arch.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/netif.h"

#define WIFI_SSID "Megalink_sonia"
#define WIFI_PASSWORD "kally#*ww"

#define BUTTONA_PIN 5
#define BUTTONB_PIN 6
const int VRX = 26; 
const int VRY = 27; 
#define JOY_X_CHANNEL 0  // GPIO 27
#define JOY_Y_CHANNEL 1 // GPIO 28

#define LED_PIN CYW43_WL_GPIO_LED_PIN


static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (!p) {
        tcp_close(tpcb);
        tcp_recv(tpcb, NULL);
        return ERR_OK;
    }

    char *request = (char *)malloc(p->len + 1);
    memcpy(request, p->payload, p->len);
    request[p->len] = '\0';

    // Leitura dos botões
    bool btna = !gpio_get(BUTTONA_PIN);
    bool btnb = !gpio_get(BUTTONB_PIN);

    // Sensor de temperatura interno (canal 4)
    adc_select_input(4);
    uint16_t raw_value = adc_read();
    const float conversion_factor = 3.3f / (1 << 12);
    float temperatura = 27.0f - ((raw_value * conversion_factor - 0.706f) / 0.001721f);

    // Leitura do joystick
    adc_select_input(JOY_X_CHANNEL);
    uint16_t joy_x = adc_read();
    adc_select_input(JOY_Y_CHANNEL);
    uint16_t joy_y = adc_read();

    // Interpretação de direção
    const char *direcao;
int deadzone = 700;  // Zona morta aumentada para evitar detecções falsas
int threshold = 3000; // Limite para considerar movimento válido

// Primeiro verificamos as diagonais (combinações de X e Y)
if (joy_x > threshold && joy_y > threshold)
    direcao = "Nordeste";
else if (joy_x < (4095 - threshold) && joy_y > threshold)
    direcao = "Noroeste";
else if (joy_x > threshold && joy_y < (4095 - threshold))
    direcao = "Sudeste";
else if (joy_x < (4095 - threshold) && joy_y < (4095 - threshold))
    direcao = "Sudoeste";

// Depois verificamos as direções cardeais
else if (joy_y > threshold && abs(joy_x - 2048) < deadzone)
    direcao = "Norte";
else if (joy_y < (4095 - threshold) && abs(joy_x - 2048) < deadzone)
    direcao = "Sul";
else if (joy_x > threshold && abs(joy_y - 2048) < deadzone)
    direcao = "Leste";
else if (joy_x < (4095 - threshold) && abs(joy_y - 2048) < deadzone)
    direcao = "Oeste";

// Se nenhuma das anteriores for verdadeira
else
    direcao = "Centro";

    // Página HTML
    char html[2048];

    snprintf(html, sizeof(html),
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html; charset=UTF-8\r\n"
    "\r\n"
    "<!DOCTYPE html>\n"
    "<html lang=\"pt\">\n"
    "<head>\n"
    "<meta charset=\"UTF-8\">\n"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
    "<title>Status do Sistema</title>\n"
    "<style>\n"
    "body { font-family: 'Segoe UI', sans-serif; background: #f0f4f8; text-align: center; padding: 30px; }\n"
    ".card { background: white; max-width: 500px; margin: auto; padding: 25px; border-radius: 12px; box-shadow: 0 4px 10px rgba(0,0,0,0.1); }\n"
    ".card h1 { margin-bottom: 20px; font-size: 26px; color: #333; }\n"
    ".status { font-size: 18px; margin: 10px 0; color: #444; }\n"
    "</style>\n"
    "</head>\n"
    "<body>\n"
    "<div class=\"card\">\n"
    "<h1>Status dos Botões e Joystick</h1>\n"
    "<p class=\"status\">Botão A: %s</p>\n"
    "<p class=\"status\">Botão B: %s</p>\n"
    "<p class=\"status\">Temperatura Interna: %.2f &deg;C</p>\n"
    "<p class=\"status\">Joystick X: %d</p>\n"
    "<p class=\"status\">Joystick Y: %d</p>\n"
    "<p class=\"status\">Direção: %s</p>\n"
    "</div>\n"
    "</body></html>\n",
    btna ? "Pressionado" : "Solto",
    btnb ? "Pressionado" : "Solto",
    temperatura,
    joy_x, joy_y,
    direcao );

    tcp_write(tpcb, html, strlen(html), TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);

    free(request);
    pbuf_free(p);

    return ERR_OK;
}

static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err) {
    tcp_recv(newpcb, tcp_server_recv);
    return ERR_OK;
}

int main() {
    stdio_init_all();

    // Botões
    gpio_init(BUTTONA_PIN); gpio_set_dir(BUTTONA_PIN, GPIO_IN); gpio_pull_up(BUTTONA_PIN);
    gpio_init(BUTTONB_PIN); gpio_set_dir(BUTTONB_PIN, GPIO_IN); gpio_pull_up(BUTTONB_PIN);

    // ADC e sensores
    adc_init();
    adc_set_temp_sensor_enabled(true); // Ativa sensor interno
    adc_gpio_init(27); // Joystick X
    adc_gpio_init(28); // Joystick Y

    // Inicializa Wi-Fi
    if (cyw43_arch_init()) {
        printf("Erro ao inicializar Wi-Fi\n");
        return -1;
    }
    cyw43_arch_enable_sta_mode();
    printf("Conectando...\n");
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 20000)) {
        printf("Falha ao conectar\n");
        return -1;
    }
    printf("Conectado ao Wi-Fi\n");
    if (netif_default) {
        printf("IP: %s\n", ipaddr_ntoa(&netif_default->ip_addr));
    }

    // Servidor
    struct tcp_pcb *server = tcp_new();
    tcp_bind(server, IP_ADDR_ANY, 80);
    server = tcp_listen(server);
    tcp_accept(server, tcp_server_accept);

    while (true) {
        cyw43_arch_poll();
        sleep_ms(1000);
    }

    cyw43_arch_deinit();
    return 0;
}
