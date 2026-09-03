/**
 * IMD0904 - Plataforma de Hardware para Internet das Coisas
 * Atividade prática 1
 *
 * Requisitos atendidos:
 *  - Espera não ocupada (sem delay/vTaskDelay bloqueante na lógica principal)
 *  - Leitura de ADC (potenciômetro e LDR, 0-1 V, ADC1_CH4 e ADC1_CH6)
 *  - Escrita via PWM (LED1, duty = leitura do ADC ativo)
 *  - Botão com pull-up interno (entrada em nível baixo quando pressionado)
 *  - LED2 pisca com período variando entre 100 ms e 1000 ms, baseado no ADC
 *
 * Pinagem (conforme diagram.json / esquema Wokwi):
 *  GPIO16 -> LED1 (via resistor r1) - saída PWM
 *  GPIO17 -> LED2 (via resistor r2) - saída digital (pisca)
 *  GPIO26 -> Botão "Modo" (outro terminal no GND)
 *  GPIO32 -> Sinal do potenciômetro (ADC1_CH4)
 *  GPIO34 -> Sinal do LDR (ADC1_CH6)
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_timer.h"
#include "esp_log.h"

/* ---------- Definições de pinos ---------- */
#define LED1_GPIO       16
#define LED2_GPIO       17
#define BTN_GPIO        26

#define POT_ADC_CHANNEL ADC_CHANNEL_4   /* GPIO32 */
#define LDR_ADC_CHANNEL ADC_CHANNEL_6   /* GPIO34 */

/* ---------- Configurações de PWM (LED1) ---------- */
#define LEDC_TIMER          LEDC_TIMER_0
#define LEDC_MODE            LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL         LEDC_CHANNEL_0
#define LEDC_DUTY_RES        LEDC_TIMER_10_BIT   /* duty de 0 a 1023 */
#define LEDC_FREQUENCY_HZ    5000

/* ---------- Parâmetros de tempo ---------- */
#define DEBOUNCE_US          50000     /* 50 ms de debounce do botão */
#define LED2_MIN_PERIOD_MS   100
#define LED2_MAX_PERIOD_MS   1000

/* ---------- Tipos ---------- */
typedef enum {
    MODO_MANUAL = 0,   /* le o potenciometro */
    MODO_AUTOMATICO     /* le o LDR */
} modo_t;

static const char *TAG = "T1";

static adc_oneshot_unit_handle_t adc1_handle;

/* Função auxiliar equivalente ao map() do Arduino */
static long map_range(long x, long in_min, long in_max, long out_min, long out_max)
{
    ESP_LOGI(TAG, "Chegou aqui5");

    if (in_max == in_min) {
        return out_min;
    }
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

static void gpio_init(void)
{
    ESP_LOGI(TAG, "Chegou aqui4");

    /* LED2 como saída digital */
    gpio_config_t led2_cfg = {
        .pin_bit_mask = (1ULL << LED2_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&led2_cfg);

    /* Botão como entrada com pull-up interno (nível baixo = pressionado) */
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << BTN_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_cfg);
}

static void pwm_init(void)
{
    ESP_LOGI(TAG, "Chegou aqui3");
    ledc_timer_config_t timer_cfg = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = LEDC_FREQUENCY_HZ,
        .clk_cfg          = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_cfg);

    ledc_channel_config_t channel_cfg = {
        .gpio_num       = LED1_GPIO,
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .duty           = 0,
        .hpoint         = 0,
    };
    ledc_channel_config(&channel_cfg);
}

static void adc_init(void)
{
    ESP_LOGI(TAG, "Chegou aqui2");

    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    adc_oneshot_new_unit(&init_cfg, &adc1_handle);

    /* Atenuação 0 dB: faixa útil de entrada de aproximadamente 0-1,1 V,
     * compatível com o divisor de tensão de 0-1 V do circuito. */
    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,   /* 12 bits -> 0-4095 */
        .atten    = ADC_ATTEN_DB_0,
    };
    adc_oneshot_config_channel(adc1_handle, POT_ADC_CHANNEL, &chan_cfg);
    adc_oneshot_config_channel(adc1_handle, LDR_ADC_CHANNEL, &chan_cfg);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Chegou aqui1");
    gpio_init();
    pwm_init();
    adc_init();

    modo_t modo_atual = MODO_MANUAL;   /* inicia em modo manual */

    /* Estado do botão / debounce (espera não ocupada) */
    int ultimo_nivel_botao = 1;        /* pull-up: solto = nível alto */
    int64_t ultimo_evento_botao_us = 0;

    /* Estado do LED2 (pisca não bloqueante) */
    bool led2_estado = false;
    int64_t ultimo_toggle_led2_us = 0;

    ESP_LOGI(TAG, "Iniciando em modo MANUAL");

    while (1) {
        int64_t agora_us = esp_timer_get_time();

        /* ---------- Leitura do botão com debounce (espera não ocupada) ---------- */
        int nivel_botao = gpio_get_level(BTN_GPIO);
        if (nivel_botao != ultimo_nivel_botao &&
            (agora_us - ultimo_evento_botao_us) > DEBOUNCE_US) {

            ultimo_evento_botao_us = agora_us;
            ultimo_nivel_botao = nivel_botao;

            /* Borda de descida (botão pressionado, pull-up) alterna o modo */
            if (nivel_botao == 0) {
                modo_atual = (modo_atual == MODO_MANUAL) ? MODO_AUTOMATICO : MODO_MANUAL;
                ESP_LOGI(TAG, "Modo alterado para: %s",
                         modo_atual == MODO_MANUAL ? "MANUAL" : "AUTOMATICO");
            }
        }

        /* ---------- Leitura do ADC de acordo com o modo ---------- */
        int leitura_adc = 0;
        if (modo_atual == MODO_MANUAL) {
            adc_oneshot_read(adc1_handle, POT_ADC_CHANNEL, &leitura_adc);
        } else {
            adc_oneshot_read(adc1_handle, LDR_ADC_CHANNEL, &leitura_adc);
        }

        /* ---------- LED1: PWM com duty proporcional à leitura ---------- */
        int duty = map_range(leitura_adc, 0, 4095, 0, 1023);
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);

        /* ---------- LED2: pisca com período entre 100 ms e 1000 ms ---------- */
        int periodo_ms = map_range(leitura_adc, 0, 4095,
                                    LED2_MAX_PERIOD_MS, LED2_MIN_PERIOD_MS);
        /* meio período = tempo entre cada troca de estado (on/off) */
        int64_t meio_periodo_us = ((int64_t)periodo_ms * 1000) / 2;

        if ((agora_us - ultimo_toggle_led2_us) >= meio_periodo_us) {
            ultimo_toggle_led2_us = agora_us;
            led2_estado = !led2_estado;
            gpio_set_level(LED2_GPIO, led2_estado);
        }

        /* Pequena cessão de CPU para o escalonador do FreeRTOS.
         * Não é um delay fixo que trava a lógica: apenas evita
         * ocupar 100% da CPU em espera ocupada. */
        vTaskDelay(1);
    }
}