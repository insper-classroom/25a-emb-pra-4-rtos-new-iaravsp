/*
 * LED blink with FreeRTOS
 */
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <queue.h>

#include "ssd1306.h"
#include "gfx.h"

#include "pico/stdlib.h"
#include <stdio.h>

const uint BTN_1_OLED = 28;
const uint BTN_2_OLED = 26;
const uint BTN_3_OLED = 27;

const uint LED_1_OLED = 20;
const uint LED_2_OLED = 21;
const uint LED_3_OLED = 22;

const int PIN_TRIGGER = 16;
const int PIN_ECHO = 18;

QueueHandle_t xQueueTime;
QueueHandle_t xQueueDistance;
SemaphoreHandle_t xSemaphoreTrigger;

void pin_callback(uint gpio, uint32_t events)
{
    int tempo = 0;
    if (events == 0x4 && gpio == PIN_ECHO)
    {
        // o ultrassom desce no 0x4, é o contrario do botão
        tempo = to_us_since_boot(get_absolute_time());
    }
    else if (events == 0x8 && gpio == PIN_ECHO)
    {
        tempo = to_us_since_boot(get_absolute_time());
    }
    xQueueSendFromISR(xQueueTime, &tempo, 0);
}
void trigger_task(void *p)
{
    gpio_init(PIN_TRIGGER);
    gpio_set_dir(PIN_TRIGGER, GPIO_OUT);

    for (;;)
    {
        int delay = 1; // ms
        gpio_put(PIN_TRIGGER, 1);
        vTaskDelay(pdMS_TO_TICKS(delay));
        gpio_put(PIN_TRIGGER, 0);
        xSemaphoreGive(xSemaphoreTrigger);
    }
}
void echo_task(void *p)
{
    gpio_init(PIN_ECHO);
    gpio_set_dir(PIN_ECHO, GPIO_IN);

    int tempo_inicial = 0;
    int tempo_final = 0;
    int duracao = 0;

    for (;;)
    {
        // aguarda tempo inf pelo primeiro tempo, parar evitar quebrar o codigo caso n venha em um tempo específico
        if (xQueueReceive(xQueueTime, &tempo_inicial, portMAX_DELAY))
        {
            if (xQueueReceive(xQueueTime, &tempo_final, pdMS_TO_TICKS(100)))
            {
                // dado pronto para o uso
                duracao = tempo_final - tempo_inicial;
                float distancia = (duracao / 2.0) * 0.03403; // do video
                xQueueSend(xQueueDistance, &distancia, 0);
            }
            else
            {
                printf("Errro ao receber tempo final\n");
            }
        }
        else
        {
            printf("Errro ao receber tempo inicial\n");
        }
    }
}
void oled_task(void *p)
{

    printf("Inicializando Driver\n");
    ssd1306_init();

    printf("Inicializando GLX\n");
    ssd1306_t disp;
    gfx_init(&disp, 128, 32);

    if (xSemaphoreTake(xSemaphoreTrigger, pdMS_TO_TICKS(500)) == pdTRUE)
    {
        for (;;)
        {
            gfx_clear_buffer(&disp);

            float distancia = 0;
            if (xQueueReceive(xQueueDistance, &distancia, pdMS_TO_TICKS(100)))
            {
                if (distancia > 2 && distancia < 400)
                {
                    char distancia_format[128];
                    sprintf(distancia_format, "Dist:  %.2fcm", distancia);
                    gfx_draw_string(&disp, 0, 0, 1, distancia_format);
                    printf("%s\n", distancia_format);
                    //variavel proporcional a distancia para passar para o drawline
                    //2-100cm
                    //0-128px
                    int prop = ((distancia - 2) * 128) / 398;
                    gfx_draw_line(&disp, 0, 27, prop, 27);
                    vTaskDelay(pdMS_TO_TICKS(10));
                    gfx_show(&disp);
                }
                else
                {
                    gfx_clear_buffer(&disp);
                    gfx_draw_string(&disp, 0, 0, 1, "Dist. fora dos limites");
                    vTaskDelay(pdMS_TO_TICKS(10));
                    gfx_show(&disp);
                }
            }
            else
            {
                 // trata o erro de falha com o timeout do receive
                // tem que ter chegado o semaforo e a distancia, se não foi falha na leitura!!!!!
                gfx_clear_buffer(&disp);
                char erro[10] = "Falha!";
                gfx_draw_string(&disp, 0, 0, 2, erro);
                vTaskDelay(pdMS_TO_TICKS(10));
                gfx_show(&disp);
            }
        };
    }
    else
    {
        gfx_clear_buffer(&disp);
        char erro[25] = "Trigger n enviado";
        gfx_draw_string(&disp, 0, 0, 1, erro);
        vTaskDelay(pdMS_TO_TICKS(150));
        gfx_show(&disp);
    }
}

int main()
{
    stdio_init_all();

    gpio_set_irq_enabled_with_callback(PIN_ECHO, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true,
                                       &pin_callback);

    xQueueTime = xQueueCreate(32, sizeof(int));
    if (xQueueTime == NULL)
        printf("falha em criar a fila de tempo\n");

    xQueueDistance = xQueueCreate(32, sizeof(float));
    if (xQueueDistance == NULL)
        printf("falha em criar a fila de distancia\n");

    xSemaphoreTrigger = xSemaphoreCreateBinary();
    if (xSemaphoreTrigger == NULL)
        printf("falha em criar o semaforo \n");

    xTaskCreate(trigger_task, "Task Trigger", 4095, NULL, 1, NULL);
    xTaskCreate(echo_task, "Task Echo", 4095, NULL, 1, NULL);
    xTaskCreate(oled_task, "Task Oled", 4095, NULL, 1, NULL);

    vTaskStartScheduler();

    while (true)
        ;
}
