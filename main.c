#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "ssd1306.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "pico/rand.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#define BUZZER_PIN 21
#define LED_PIN_GREEN 11
#define LED_PIN_RED 13
#define JOYSTICK_X 26
#define JOYSTICK_Y 27
#define BUTTON_R 6

#define ADC_CHANNEL_0 0
#define ADC_CHANNEL_1 1

#define PWM_PERIOD 2000
#define PWM_DIVIDER 16.0
#define PWM_LED_LEVEL 100

#define NUM_LINES 4
#define NUMBERS_PER_LINE 4
#define PIN_LENGTH 6
#define DEBOUNCE_TIME_MS 200
#define TOTAL_CHARS 16

typedef enum {
    EVENTO_NAVEGACAO,
    EVENTO_SELECAO
} EventoEntradaTipo_t;

typedef struct {
    EventoEntradaTipo_t tipo;
    uint8_t linha;
} InputEvent_t;

typedef struct {
    uint8_t etapa;
} RandomizerRequest_t;

typedef struct {
    uint8_t etapa;
    char matriz[NUM_LINES][NUMBERS_PER_LINE];
} RandomizerResponse_t;

typedef enum {
    DISP_ATUALIZAR_MATRIZ,
    DISP_ATUALIZAR_SELECAO,
    DISP_ATUALIZAR_SENHA,
    DISP_MENSAGEM
} DisplayCommandType_t;

typedef struct {
    DisplayCommandType_t tipo;
    union {
        char matriz[NUM_LINES][NUMBERS_PER_LINE];
        uint8_t linha;
        char senha[PIN_LENGTH+1];
        char mensagem[30];
    } data;
} DisplayCommand_t;

typedef struct {
    bool sucesso;
} AuthResult_t;

QueueHandle_t xQueueInput;
QueueHandle_t xQueueRandomizerRequest;
QueueHandle_t xQueueRandomizerResponse;
QueueHandle_t xQueueDisplay;
QueueHandle_t xQueueAuthResult;
SemaphoreHandle_t xSemaphoreButton;
SemaphoreHandle_t xMutexMatriz;

static char matrizes_digitos[PIN_LENGTH][NUM_LINES][NUMBERS_PER_LINE];
ssd1306_t disp;
uint8_t global_linha_selecionada = 0;

void inicializar_display(void);
void inicializar_joystick(void);
void inicializar_pwm_led(uint led_pin);
void inicializar_pwm_buzzer(uint pin);
void emitir_beep(uint pin, uint frequencia, uint duracao_ms);
void mostrar_selecao(ssd1306_t *disp, uint8_t linha);
void limpar_area_selecao(ssd1306_t *disp);

/**
 * @brief Rotina de serviço de interrupção do botão.
 * @param gpio Número do GPIO que causou a interrupção.
 * @param events Máscara de eventos associados à interrupção.
 */
static void button_isr(uint gpio, uint32_t events) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (gpio == BUTTON_R) {
        xSemaphoreGiveFromISR(xSemaphoreButton, &xHigherPriorityTaskWoken);
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/**
 * @brief Tarefa que lê o joystick e trata pressionamentos de botão.
 * @param pvParameters Ponteiro passado na criação da tarefa (não utilizado).
 */
void task_input(void *pvParameters) {
    uint16_t valor_x = 0;
    TickType_t last_button_time = 0;
    TickType_t last_move_time = 0;
    uint8_t current_line = 0;
    const TickType_t debounce_move = pdMS_TO_TICKS(200);
    
    while (1) {
        adc_select_input(ADC_CHANNEL_0);
        valor_x = adc_read();
        
        InputEvent_t evento;
        bool movimento = false;
        TickType_t current_time = xTaskGetTickCount();
        
        if (valor_x < 1500 || valor_x > 2500) {
            if ((current_time - last_move_time) >= debounce_move) {
                if (valor_x < 1500 && current_line < NUM_LINES - 1) {
                    evento.tipo = EVENTO_NAVEGACAO;
                    evento.linha = ++current_line;
                    movimento = true;
                } else if (valor_x > 2500 && current_line > 0) {
                    evento.tipo = EVENTO_NAVEGACAO;
                    evento.linha = --current_line;
                    movimento = true;
                }
            }
        }

        if (movimento) {
            xQueueSend(xQueueInput, &evento, 0);
            last_move_time = current_time;
        }

        if (xSemaphoreTake(xSemaphoreButton, 0) == pdTRUE) {
            if (current_time - last_button_time > pdMS_TO_TICKS(DEBOUNCE_TIME_MS)) {
                last_button_time = current_time;
                evento.tipo = EVENTO_SELECAO;
                evento.linha = current_line;
                xQueueSend(xQueueInput, &evento, 0);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

/**
 * @brief Tarefa responsável por embaralhar e gerar matrizes do teclado.
 * @param pvParameters Ponteiro passado na criação da tarefa (não utilizado).
 */
void task_randomizer(void *pvParameters) {
    RandomizerRequest_t request;
    char matriz_flat[TOTAL_CHARS];
    
    while (1) {
        if (xQueueReceive(xQueueRandomizerRequest, &request, portMAX_DELAY)) {
            const char chars[] = {'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};
            memcpy(matriz_flat, chars, TOTAL_CHARS);
            
            for (size_t i = 0; i < TOTAL_CHARS - 1; i++) {
                size_t j = i + (get_rand_32() % (TOTAL_CHARS - i));
                char t = matriz_flat[j];
                matriz_flat[j] = matriz_flat[i];
                matriz_flat[i] = t;
            }
            
            RandomizerResponse_t response;
            response.etapa = request.etapa;
            
            int index = 0;
            for (int i = 0; i < NUM_LINES; i++) {
                for (int j = 0; j < NUMBERS_PER_LINE; j++) {
                    char novo_caractere = matriz_flat[index++];
                    bool duplicata = false;
                    
                    for (int k = 0; k < i; k++) {
                        for (int l = 0; l < NUMBERS_PER_LINE; l++) {
                            if (response.matriz[k][l] == novo_caractere) {
                                duplicata = true;
                                break;
                            }
                        }
                        if (duplicata) break;
                    }
                    
                    int tentativas = 0;
                    while (duplicata && tentativas < 10) {
                        if (index >= TOTAL_CHARS) {
                            index = 0;
                            for (size_t idx = 0; idx < TOTAL_CHARS - 1; idx++) {
                                size_t jdx = idx + (get_rand_32() % (TOTAL_CHARS - idx));
                                char t = matriz_flat[jdx];
                                matriz_flat[jdx] = matriz_flat[idx];
                                matriz_flat[idx] = t;
                            }
                        }
                        novo_caractere = matriz_flat[index++];
                        duplicata = false;
                        for (int k = 0; k < i; k++) {
                            for (int l = 0; l < NUMBERS_PER_LINE; l++) {
                                if (response.matriz[k][l] == novo_caractere) {
                                    duplicata = true;
                                    break;
                                }
                            }
                            if (duplicata) break;
                        }
                        tentativas++;
                    }
                    response.matriz[i][j] = novo_caractere;
                }
            }
            
            xSemaphoreTake(xMutexMatriz, portMAX_DELAY);
            memcpy(matrizes_digitos[request.etapa], response.matriz, sizeof(response.matriz));
            xSemaphoreGive(xMutexMatriz);
            
            xQueueSend(xQueueRandomizerResponse, &response, portMAX_DELAY);
        }
    }
}

/**
 * @brief Limpa a área de seleção do teclado no display.
 * @param disp Ponteiro para a instância do display.
 */
void limpar_area_selecao(ssd1306_t *disp) {
    ssd1306_clear_square(disp, 0, 0, 15, 64);
}

/**
 * @brief Desenha o indicador de seleção em uma linha do display.
 * @param disp Ponteiro para a instância do display.
 * @param linha Índice da linha a destacar (0–3).
 */
void mostrar_selecao(ssd1306_t *disp, uint8_t linha) {
    uint32_t x = 10;
    uint32_t y;
    
    switch (linha) {
        case 0: y = 5; break;
        case 1: y = 20; break;
        case 2: y = 35; break;
        case 3: y = 50; break;
        default: return;
    }
    
    ssd1306_draw_square(disp, x, y, 2, 5);
    ssd1306_draw_square(disp, x + 1, y + 1, 2, 3);
    ssd1306_draw_square(disp, x + 2, y + 2, 2, 1);
}

/**
 * @brief Tarefa responsável por atualizar o display OLED.
 * @param pvParameters Ponteiro passado na criação da tarefa (não utilizado).
 */
void task_display(void *pvParameters) {
    inicializar_display();
    uint8_t current_line = 0;
    uint8_t last_line = 0xFF;
    char senha_display[PIN_LENGTH+1] = {0};
    bool matriz_visivel = true;
    
    while (1) {
        DisplayCommand_t cmd;
        if (xQueueReceive(xQueueDisplay, &cmd, portMAX_DELAY)) {
            switch (cmd.tipo) {
                case DISP_ATUALIZAR_MATRIZ: {
                    matriz_visivel = true;
                    char buffer[20];
                    ssd1306_clear(&disp);
                    
                    for (int i = 0; i < NUM_LINES; i++) {
                        sprintf(buffer, "%c %c %c %c",
                                cmd.data.matriz[i][0], cmd.data.matriz[i][1],
                                cmd.data.matriz[i][2], cmd.data.matriz[i][3]);
                        ssd1306_draw_string(&disp, 25, 5 + 15*i, 1, buffer);
                    }
                    
                    if (senha_display[0] != '\0') {
                        ssd1306_draw_string(&disp, 80, 27, 1, senha_display);
                    }
                    
                    if (last_line != current_line) {
                        limpar_area_selecao(&disp);
                        mostrar_selecao(&disp, current_line);
                        last_line = current_line;
                    } else {
                        mostrar_selecao(&disp, current_line);
                    }
                    ssd1306_show(&disp);
                    break;
                }
                case DISP_ATUALIZAR_SELECAO:
                    if (matriz_visivel) {
                        limpar_area_selecao(&disp);
                        current_line = cmd.data.linha;
                        mostrar_selecao(&disp, current_line);
                        last_line = current_line;
                        ssd1306_show(&disp);
                    }
                    break;
                case DISP_ATUALIZAR_SENHA:
                    if (matriz_visivel) {
                        strncpy(senha_display, cmd.data.senha, sizeof(senha_display));
                        ssd1306_clear_square(&disp, 80, 27, 48, 8);
                        ssd1306_draw_string(&disp, 80, 27, 1, senha_display);
                        ssd1306_show(&disp);
                    } else {
                        strncpy(senha_display, cmd.data.senha, sizeof(senha_display));
                    }
                    break;
                case DISP_MENSAGEM:
                    matriz_visivel = false;
                    ssd1306_clear(&disp);
                    ssd1306_draw_string(&disp, 15, 30, 1, cmd.data.mensagem);
                    ssd1306_show(&disp);
                    break;
            }
        }
    }
}

/**
 * @brief Tarefa que gerencia o fluxo de autenticação e validação de senha.
 * @param pvParameters Ponteiro passado na criação da tarefa (não utilizado).
 */
void task_auth(void *pvParameters) {
    uint8_t char_count = 0;
    char senha_display[PIN_LENGTH+1] = {0};
    uint8_t linhas_selecionadas[PIN_LENGTH] = {0};
    
    RandomizerRequest_t request = { .etapa = 0 };
    xQueueSend(xQueueRandomizerRequest, &request, 0);
    
    RandomizerResponse_t response;
    if (xQueueReceive(xQueueRandomizerResponse, &response, pdMS_TO_TICKS(1000))) {
        DisplayCommand_t cmd_matriz = { .tipo = DISP_ATUALIZAR_MATRIZ };
        memcpy(cmd_matriz.data.matriz, response.matriz, sizeof(response.matriz));
        xQueueSend(xQueueDisplay, &cmd_matriz, 0);
        
        DisplayCommand_t cmd_sel = { .tipo = DISP_ATUALIZAR_SELECAO, .data.linha = 0 };
        xQueueSend(xQueueDisplay, &cmd_sel, 0);
        global_linha_selecionada = 0;
    }
    
    while (1) {
        InputEvent_t evento;
        if (xQueueReceive(xQueueInput, &evento, portMAX_DELAY)) {
            if (evento.tipo == EVENTO_NAVEGACAO) {
                global_linha_selecionada = evento.linha;
                DisplayCommand_t cmd = { .tipo = DISP_ATUALIZAR_SELECAO, .data.linha = global_linha_selecionada };
                xQueueSend(xQueueDisplay, &cmd, 0);
            } else if (evento.tipo == EVENTO_SELECAO) {
                if (char_count < PIN_LENGTH) {
                    linhas_selecionadas[char_count] = global_linha_selecionada;
                    senha_display[char_count] = '*';
                    senha_display[char_count + 1] = '\0';
                    char_count++;
                    
                    DisplayCommand_t cmd_senha = { .tipo = DISP_ATUALIZAR_SENHA };
                    strncpy(cmd_senha.data.senha, senha_display, PIN_LENGTH+1);
                    xQueueSend(xQueueDisplay, &cmd_senha, 0);
                    
                    if (char_count < PIN_LENGTH) {
                        RandomizerRequest_t req = { .etapa = char_count };
                        xQueueSend(xQueueRandomizerRequest, &req, 0);
                        
                        RandomizerResponse_t resp;
                        if (xQueueReceive(xQueueRandomizerResponse, &resp, pdMS_TO_TICKS(1000))) {
                            DisplayCommand_t cmd_matriz = { .tipo = DISP_ATUALIZAR_MATRIZ };
                            memcpy(cmd_matriz.data.matriz, resp.matriz, sizeof(resp.matriz));
                            xQueueSend(xQueueDisplay, &cmd_matriz, 0);
                            
                            DisplayCommand_t cmd_sel = { .tipo = DISP_ATUALIZAR_SELECAO, .data.linha = global_linha_selecionada };
                            xQueueSend(xQueueDisplay, &cmd_sel, 0);
                        }
                    } else {
                        bool senha_valida = true;
                        const char senha_correta[PIN_LENGTH] = {'1','2','3','4','5','6'};
                        
                        for (int i = 0; i < PIN_LENGTH; i++) {
                            bool digito_valido = false;
                            
                            xSemaphoreTake(xMutexMatriz, portMAX_DELAY);
                            for (int j = 0; j < NUMBERS_PER_LINE; j++) {
                                if (matrizes_digitos[i][linhas_selecionadas[i]][j] == senha_correta[i]) {
                                    digito_valido = true;
                                    break;
                                }
                            }
                            xSemaphoreGive(xMutexMatriz);
                            
                            if (!digito_valido) {
                                senha_valida = false;
                                break;
                            }
                        }
                        
                        AuthResult_t result = { .sucesso = senha_valida };
                        xQueueSend(xQueueAuthResult, &result, 0);
                        
                        DisplayCommand_t cmd_msg = { .tipo = DISP_MENSAGEM };
                        strcpy(cmd_msg.data.mensagem,
                               senha_valida ? "SENHA CORRETA" : "SENHA INCORRETA");
                        xQueueSend(xQueueDisplay, &cmd_msg, 0);
                        
                        vTaskDelay(pdMS_TO_TICKS(2000));
                        
                        char_count = 0;
                        global_linha_selecionada = 0;
                        memset(senha_display, 0, sizeof(senha_display));
                        memset(linhas_selecionadas, 0, sizeof(linhas_selecionadas));
                        
                        RandomizerRequest_t req = { .etapa = 0 };
                        xQueueSend(xQueueRandomizerRequest, &req, 0);
                        
                        RandomizerResponse_t resp;
                        if (xQueueReceive(xQueueRandomizerResponse, &resp, pdMS_TO_TICKS(1000))) {
                            DisplayCommand_t cmd_matriz = { .tipo = DISP_ATUALIZAR_MATRIZ };
                            memcpy(cmd_matriz.data.matriz, resp.matriz, sizeof(resp.matriz));
                            xQueueSend(xQueueDisplay, &cmd_matriz, 0);
                            
                            DisplayCommand_t cmd_sel = { .tipo = DISP_ATUALIZAR_SELECAO, .data.linha = 0 };
                            xQueueSend(xQueueDisplay, &cmd_sel, 0);
                            global_linha_selecionada = 0;
                        }
                        
                        DisplayCommand_t cmd_clear = { .tipo = DISP_ATUALIZAR_SENHA };
                        strncpy(cmd_clear.data.senha, "", PIN_LENGTH+1);
                        xQueueSend(xQueueDisplay, &cmd_clear, 0);
                    }
                }
            }
        }
    }
}

/**
 * @brief Tarefa que reproduz feedback de áudio conforme resultado da autenticação.
 * @param pvParameters Ponteiro passado na criação da tarefa (não utilizado).
 */
void task_audio(void *pvParameters) {
    inicializar_pwm_led(LED_PIN_GREEN);
    inicializar_pwm_led(LED_PIN_RED);
    inicializar_pwm_buzzer(BUZZER_PIN);
    
    while (1) {
        AuthResult_t result;
        if (xQueueReceive(xQueueAuthResult, &result, portMAX_DELAY)) {
            inicializar_pwm_buzzer(BUZZER_PIN);
            if (result.sucesso) {
                pwm_set_gpio_level(LED_PIN_GREEN, PWM_LED_LEVEL);
                emitir_beep(BUZZER_PIN, 523, 250);
                vTaskDelay(pdMS_TO_TICKS(50));
                emitir_beep(BUZZER_PIN, 659, 250);
                vTaskDelay(pdMS_TO_TICKS(50));
                emitir_beep(BUZZER_PIN, 784, 250);
                vTaskDelay(pdMS_TO_TICKS(50));
                emitir_beep(BUZZER_PIN, 659, 250);
                vTaskDelay(pdMS_TO_TICKS(50));
                emitir_beep(BUZZER_PIN, 784, 500);
                vTaskDelay(pdMS_TO_TICKS(100));
                emitir_beep(BUZZER_PIN, 880, 500);
                pwm_set_gpio_level(BUZZER_PIN, 0);
                pwm_set_gpio_level(LED_PIN_GREEN, 0);
            } else {
                pwm_set_gpio_level(LED_PIN_RED, PWM_LED_LEVEL);
                emitir_beep(BUZZER_PIN, 392, 500);
                vTaskDelay(pdMS_TO_TICKS(50));
                emitir_beep(BUZZER_PIN, 330, 750);
                pwm_set_enabled(pwm_gpio_to_slice_num(BUZZER_PIN), false);
                gpio_set_function(BUZZER_PIN, GPIO_FUNC_NULL);
                pwm_set_gpio_level(LED_PIN_RED, 0);
            }
        }
    }
}

/**
 * @brief Gera um beep por PWM no pino especificado.
 * @param pin Pino GPIO conectado ao buzzer.
 * @param frequencia Frequência em Hz.
 * @param duracao_ms Duração em milissegundos.
 */
void emitir_beep(uint pin, uint frequencia, uint duracao_ms) {
    if (frequencia == 0) {
        vTaskDelay(pdMS_TO_TICKS(duracao_ms));
        return;
    }

    uint slice_num = pwm_gpio_to_slice_num(pin);
    uint chan = pwm_gpio_to_channel(pin);
    float divisor = (float)clock_get_hz(clk_sys) / (frequencia * 4096.0f);
    
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, divisor);
    pwm_config_set_wrap(&config, 4095);
    pwm_init(slice_num, &config, true);
    
    pwm_set_chan_level(slice_num, chan, 2048);
    vTaskDelay(pdMS_TO_TICKS(duracao_ms));
    
    pwm_set_enabled(slice_num, false);
    gpio_set_function(pin, GPIO_FUNC_NULL);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, 0);
    gpio_set_function(pin, GPIO_FUNC_PWM);
}

/**
 * @brief Inicializa o slice PWM para saída do buzzer.
 * @param pin Pino GPIO a configurar para PWM.
 */
void inicializar_pwm_buzzer(uint pin) {
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(pin);
    pwm_config config = pwm_get_default_config();
    pwm_init(slice_num, &config, false);
    pwm_set_gpio_level(pin, 0);
}

/**
 * @brief Inicializa o display OLED via I2C.
 */
void inicializar_display(void) {
    i2c_init(i2c1, 400000);
    gpio_set_function(14, GPIO_FUNC_I2C);
    gpio_set_function(15, GPIO_FUNC_I2C);
    gpio_pull_up(14);
    gpio_pull_up(15);
    
    disp.external_vcc = false;
    ssd1306_init(&disp, 128, 64, 0x3C, i2c1);
    ssd1306_clear(&disp);
    ssd1306_show(&disp);
}

/**
 * @brief Inicializa entradas ADC para o joystick.
 */
void inicializar_joystick(void) {
    adc_init();
    adc_gpio_init(JOYSTICK_X);
    adc_gpio_init(JOYSTICK_Y);
    adc_select_input(ADC_CHANNEL_0);
}

/**
 * @brief Configura um pino GPIO para saída PWM de LED.
 * @param led_pin Pino GPIO conectado ao LED.
 */
void inicializar_pwm_led(uint led_pin) {
    uint slice = pwm_gpio_to_slice_num(led_pin);
    gpio_set_function(led_pin, GPIO_FUNC_PWM);
    
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, PWM_DIVIDER);
    pwm_config_set_wrap(&config, PWM_PERIOD);
    
    pwm_init(slice, &config, true);
    pwm_set_gpio_level(led_pin, 0);
}

/**
 * @brief Cria filas, semáforos, configura ISR do botão e inicia as tarefas.
 */
void init_freertos() {
    xQueueInput = xQueueCreate(10, sizeof(InputEvent_t));
    xQueueRandomizerRequest = xQueueCreate(5, sizeof(RandomizerRequest_t));
    xQueueRandomizerResponse = xQueueCreate(5, sizeof(RandomizerResponse_t));
    xQueueDisplay = xQueueCreate(10, sizeof(DisplayCommand_t));
    xQueueAuthResult = xQueueCreate(3, sizeof(AuthResult_t));
    xSemaphoreButton = xSemaphoreCreateBinary();
    xMutexMatriz = xSemaphoreCreateMutex();
    
    gpio_init(BUTTON_R);
    gpio_set_dir(BUTTON_R, GPIO_IN);
    gpio_pull_up(BUTTON_R);
    gpio_set_irq_enabled_with_callback(BUTTON_R, GPIO_IRQ_EDGE_FALL, true, &button_isr);
    
    xTaskCreate(task_input, "Input", 512, NULL, 3, NULL);
    xTaskCreate(task_randomizer, "Randomizer", 512, NULL, 2, NULL);
    xTaskCreate(task_display, "Display", 1024, NULL, 4, NULL);
    xTaskCreate(task_auth, "Auth", 1024, NULL, 5, NULL);
    xTaskCreate(task_audio, "Audio", 512, NULL, 3, NULL);
    
    const char chars[] = {'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};
    for (int i = 0; i < PIN_LENGTH; i++) {
        for (int j = 0; j < NUM_LINES; j++) {
            for (int k = 0; k < NUMBERS_PER_LINE; k++) {
                matrizes_digitos[i][j][k] = chars[j * NUMBERS_PER_LINE + k];
            }
        }
    }
}

/**
 * @brief Ponto de entrada principal: inicializa hardware e inicia o RTOS.
 * @return Não retorna.
 */
int main() {
    stdio_init_all();
    inicializar_joystick();
    adc_init();
    init_freertos();
    vTaskStartScheduler();

    while (1) {
        tight_loop_contents();
    }
}
