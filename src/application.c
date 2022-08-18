#include <application.h>
#include <stm32l0xx.h>

#define RED_BUTTON_GPIO TWR_GPIO_P4
#define BLUE_BUTTON_GPIO TWR_GPIO_P5
#define PIEZO_GPIO TWR_GPIO_P6

#define BRIGHTNESS_RED 40
#define BRIGHTNESS_BLUE 40
#define BRIGHTNESS_WHITE_GAP 40

#define NUMBER_OF_ROUNDS 20
#define PIEZO_BEEP_TIME 300
#define PIEZO_BEEP_MODIFIER 40
#define LED_COUNT 204
#define LED_COUNT_PER_POINT ((float)((LED_COUNT - 1.f) / NUMBER_OF_ROUNDS))

typedef struct
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t white;

} colors_t;

int score_red;
int score_blue;

bool effect_in_progress;

twr_led_strip_t led_strip;
twr_button_t button_red;
twr_button_t button_blue;
twr_button_t button_reset_red;
twr_button_t button_reset_blue;

twr_scheduler_task_id_t game_reset_task_id;

// Custom LED strip buffer
uint32_t dma_buffer_rgb_204[LED_COUNT * sizeof(uint32_t) * 2];

const twr_led_strip_buffer_t _led_strip_buffer_rgbw_204 =
{
    .type = TWR_LED_STRIP_TYPE_RGBW,
    .count = LED_COUNT,
    .buffer = dma_buffer_rgb_204
};

colors_t framebuffer[LED_COUNT];

void twr_piezo_init(void)
{
    // Initialize GPIO pin
    RCC->IOPENR |= RCC_IOPENR_GPIOBEN;

    // Errata workaround
    RCC->IOPENR;

    GPIOB->MODER &= ~GPIO_MODER_MODE1_Msk;
    GPIOB->MODER |= GPIO_MODER_MODE1_1;
    GPIOB->OSPEEDR |= GPIO_OSPEEDER_OSPEED1;
    GPIOB->AFR[0] &= GPIO_AFRL_AFRL1_Msk;
    GPIOB->AFR[0] |= 2 << GPIO_AFRL_AFRL1_Pos;

    // Initialize timer (for PWM)
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;

    // Errata workaround
    RCC->APB1ENR;

    TIM3->PSC = 210;
    TIM3->ARR = PIEZO_BEEP_MODIFIER;
    TIM3->CCR4 = PIEZO_BEEP_MODIFIER / 2;
    TIM3->CCMR2 |= TIM_CCMR2_OC4M_2 | TIM_CCMR2_OC4M_1 | TIM_CCMR2_OC4PE;
    TIM3->CCER |= TIM_CCER_CC4E;
    TIM3->EGR |= TIM_EGR_UG;
}

void piezo_beep(void)
{
    twr_tick_t tick_end = twr_tick_get() + PIEZO_BEEP_TIME;

    // Start PWM
    TIM3->CR1 |= TIM_CR1_CEN;

    // Until desired time elapsed...
    while (twr_tick_get() < tick_end)
    {
        continue;
    }

    // Stop PWM
    TIM3->CR1 &= ~TIM_CR1_CEN;
}

void update_led_strip(void)
{
    size_t i;

    memset(framebuffer, 0, sizeof(framebuffer));

    for (i = 0; i < (score_red * LED_COUNT_PER_POINT); i++)
    {
        framebuffer[i].red = BRIGHTNESS_RED;
    }

    for (i = 0; i < (score_blue * LED_COUNT_PER_POINT); i++)
    {
        framebuffer[i].blue = BRIGHTNESS_BLUE;
    }

    for (i = 0; i < ((score_red < score_blue ? score_red : score_blue) * LED_COUNT_PER_POINT); i++)
    {
        framebuffer[i].red = 0;
        framebuffer[i].green = 0;
        framebuffer[i].blue = 0;
        framebuffer[i].white = BRIGHTNESS_WHITE_GAP / 3;
    }

    for (i = 0; i <= LED_COUNT / LED_COUNT_PER_POINT; i++)
    {
        framebuffer[(uint8_t) (i * LED_COUNT_PER_POINT)].red = 0;
        framebuffer[(uint8_t) (i * LED_COUNT_PER_POINT)].green = 0;
        framebuffer[(uint8_t) (i * LED_COUNT_PER_POINT)].blue = 0;
        framebuffer[(uint8_t) (i * LED_COUNT_PER_POINT)].white = BRIGHTNESS_WHITE_GAP;
    }

    twr_led_strip_set_rgbw_framebuffer(&led_strip, (uint8_t *) framebuffer, LED_COUNT * 4);

    twr_led_strip_write(&led_strip);
}

void game_reset_task(void *param)
{
    (void) param;

    effect_in_progress = false;

    twr_led_strip_effect_stop(&led_strip);

    score_red = 0;
    score_blue = 0;

    update_led_strip();
}

void button_score_event_handler(twr_button_t *self, twr_button_event_t event, void *event_param)
{
    (void) event_param;

    int *score = self == &button_blue ? &score_blue : &score_red;

    if (!effect_in_progress)
    {
        if (event == TWR_BUTTON_EVENT_CLICK)
        {
            piezo_beep();

            if (*score >= NUMBER_OF_ROUNDS)
            {
                effect_in_progress = true;

                twr_led_strip_effect_theater_chase(&led_strip, self == &button_blue ? 0x8000 : 0x80000000, 100);
                twr_led_strip_write(&led_strip);

                twr_scheduler_plan_relative(game_reset_task_id, 3000);
            }
            else
            {
                (*score)++;
            }
        }

        if (event == TWR_BUTTON_EVENT_HOLD)
        {
            piezo_beep();

            if (*score > 0)
            {
                (*score)--;
            }
        }

        update_led_strip();
    }
}

void button_reset_event_handler(twr_button_t *self, twr_button_event_t event, void *event_param)
{
    (void) self;
    (void) event_param;

    if (event == TWR_BUTTON_EVENT_HOLD)
    {
        piezo_beep();

        game_reset_task(NULL);
    }
}

void application_init(void)
{
    twr_system_pll_enable();

    twr_tca9534a_t expander;

    twr_tca9534a_init(&expander, TWR_I2C_I2C0, 0x3e);
    twr_tca9534a_set_port_direction(&expander, 0);
    twr_tca9534a_write_port(&expander, 0x60);

    // Initialize Power Module with LED strip
    twr_module_power_init();
    twr_led_strip_init(&led_strip, twr_module_power_get_led_strip_driver(), &_led_strip_buffer_rgbw_204);

    // Initialize red button
    twr_button_init(&button_red, RED_BUTTON_GPIO, TWR_GPIO_PULL_UP, true);
    twr_button_set_event_handler(&button_red, button_score_event_handler, NULL);

    // Initialize reset red button
    twr_button_init(&button_reset_red, RED_BUTTON_GPIO, TWR_GPIO_PULL_UP, true);
    twr_button_set_event_handler(&button_reset_red, button_reset_event_handler, NULL);
    twr_button_set_hold_time(&button_reset_red, 4000);

    // Initialize blue button
    twr_button_init(&button_blue, BLUE_BUTTON_GPIO, TWR_GPIO_PULL_UP, true);
    twr_button_set_event_handler(&button_blue, button_score_event_handler, NULL);

    // Initialize reset blue button
    twr_button_init(&button_reset_blue, BLUE_BUTTON_GPIO, TWR_GPIO_PULL_UP, true);
    twr_button_set_event_handler(&button_reset_blue, button_reset_event_handler, NULL);
    twr_button_set_hold_time(&button_reset_blue, 4000);

    // Initialize piezo
    twr_piezo_init();

    game_reset_task_id = twr_scheduler_register(game_reset_task, NULL, TWR_TICK_INFINITY);

    game_reset_task(NULL);
}
