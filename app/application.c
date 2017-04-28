#include <application.h>
#include <stm32l0xx.h>

#define RED_BUTTON_GPIO BC_GPIO_P4
#define BLUE_BUTTON_GPIO BC_GPIO_P5
#define PIEZO_GPIO BC_GPIO_P6

#define BRIGHTNESS_RED 40
#define BRIGHTNESS_BLUE 40
#define BRIGHTNESS_WHITE_GAP 40

#define NUMBER_OF_ROUNDS 20
#define PIEZO_BEEP_TIME 300
#define PIEZO_BEEP_MODIFIER 40
#define LED_COUNT 204
#define LED_COUNT_PER_POINT ((float)((LED_COUNT - 1.) / NUMBER_OF_ROUNDS))

int score_red;
int score_blue;

bc_led_strip_t led_strip;
bc_button_t button_red;
bc_button_t button_blue;
bc_button_t button_reset_red;
bc_button_t button_reset_blue;

bool effect_in_progress;

bc_scheduler_task_id_t game_reset_task_id;

// Create custom led strip buffer
uint32_t _dma_buffer_rgb_204[LED_COUNT * sizeof(uint32_t) * 2];

const bc_led_strip_buffer_t _led_strip_buffer_rgbw_204 =
{
    .type = BC_LED_STRIP_TYPE_RGBW,
    .count = LED_COUNT,
    .buffer = _dma_buffer_rgb_204
};

typedef struct
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t white;

} colors_t;

colors_t frame_buffer[LED_COUNT];

void bc_piezo_init(void)
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
    bc_tick_t tick_end = bc_tick_get() + PIEZO_BEEP_TIME;

    // Start PWM
    TIM3->CR1 |= TIM_CR1_CEN;

    // Wait PIEZO_BEEP_TIME ms
    while (tick_end > bc_tick_get())
    {
        continue;
    }

    // Stop PWM
    TIM3->CR1 &= ~TIM_CR1_CEN;
}

void update_led_strip(void)
{
    size_t i;

    memset(frame_buffer, 0, sizeof(frame_buffer));

    for (i = 0; i < (score_red * LED_COUNT_PER_POINT); i++)
    {
        frame_buffer[i].red = BRIGHTNESS_RED;
    }

    for (i = 0; i < (score_blue * LED_COUNT_PER_POINT); i++)
    {
        frame_buffer[i].blue = BRIGHTNESS_BLUE;
    }

    for (i = 0; i < ((score_red < score_blue ? score_red : score_blue) * LED_COUNT_PER_POINT); i++)
    {
        frame_buffer[i].red = 0;
        frame_buffer[i].green = 0;
        frame_buffer[i].blue = 0;
        frame_buffer[i].white = BRIGHTNESS_WHITE_GAP / 3;
    }

    for (i = 0; i <= LED_COUNT / LED_COUNT_PER_POINT; i++)
    {
        frame_buffer[(uint8_t) (i * LED_COUNT_PER_POINT)].red = 0;
        frame_buffer[(uint8_t) (i * LED_COUNT_PER_POINT)].green = 0;
        frame_buffer[(uint8_t) (i * LED_COUNT_PER_POINT)].blue = 0;
        frame_buffer[(uint8_t) (i * LED_COUNT_PER_POINT)].white = BRIGHTNESS_WHITE_GAP;
    }

    bc_led_strip_set_rgbw_framebuffer(&led_strip, (uint8_t *) frame_buffer, LED_COUNT * 4);

    bc_led_strip_write(&led_strip);
}

void game_reset_task(void *param)
{
    (void) param;

    effect_in_progress = false;

    bc_led_strip_effect_stop(&led_strip);

    score_red = 0;
    score_blue = 0;

    update_led_strip();
}

void button_score_event_handler(bc_button_t *self, bc_button_event_t event, void *event_param)
{
    (void) event_param;

    int *score = self == &button_blue ? &score_blue : &score_red;

    if (!effect_in_progress)
    {
        if (event == BC_BUTTON_EVENT_CLICK)
        {
            piezo_beep();

            if (*score >= NUMBER_OF_ROUNDS)
            {
                effect_in_progress = true;

                bc_led_strip_effect_theater_chase(&led_strip, self == &button_blue ? 0x8000 : 0x80000000, 100);
                bc_led_strip_write(&led_strip);

                bc_scheduler_plan_relative(game_reset_task_id, 3000);
            }
            else
            {
                (*score)++;
            }
        }

        if (event == BC_BUTTON_EVENT_HOLD)
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

void button_reset_event_handler(bc_button_t *self, bc_button_event_t event, void *event_param)
{
    (void) self;
    (void) event_param;

    if (event == BC_BUTTON_EVENT_HOLD)
    {
        piezo_beep();

        game_reset_task(NULL);
    }
}

void application_init(void)
{
    bc_module_core_pll_enable();

    bc_tca9534a_t expander;

    bc_tca9534a_init(&expander, BC_I2C_I2C0, 0x3e);
    bc_tca9534a_set_port_direction(&expander, 0);
    bc_tca9534a_write_port(&expander, 0x60);

    // Initialize Power Module with LED strip
    bc_module_power_init();
    bc_led_strip_init(&led_strip, bc_module_power_get_led_strip_driver(), &_led_strip_buffer_rgbw_204);

    // Initialize red button
    bc_button_init(&button_red, RED_BUTTON_GPIO, BC_GPIO_PULL_UP, true);
    bc_button_set_event_handler(&button_red, button_score_event_handler, NULL);

    // Initialize reset red button
    bc_button_init(&button_reset_red, RED_BUTTON_GPIO, BC_GPIO_PULL_UP, true);
    bc_button_set_event_handler(&button_reset_red, button_reset_event_handler, NULL);
    bc_button_set_hold_time(&button_reset_red, 4000);

    // Initialize blue button
    bc_button_init(&button_blue, BLUE_BUTTON_GPIO, BC_GPIO_PULL_UP, true);
    bc_button_set_event_handler(&button_blue, button_score_event_handler, NULL);

    // Initialize reset blue button
    bc_button_init(&button_reset_blue, BLUE_BUTTON_GPIO, BC_GPIO_PULL_UP, true);
    bc_button_set_event_handler(&button_reset_blue, button_reset_event_handler, NULL);
    bc_button_set_hold_time(&button_reset_blue, 4000);

    // Initialize piezo
    bc_piezo_init();

    game_reset_task_id = bc_scheduler_register(game_reset_task, NULL, BC_TICK_INFINITY);

    game_reset_task(NULL);
}
