/*
 Visual Studio Code
 Ctrl+Shift+B     to build
 Ctrl+P task dfu  to flash MCU with dfu-util
 */

#include <application.h>
#include <bc_button.h>
#include <bc_module_power.h>
#include <bc_gpio.h>
#include <bcl.h>

#define RED_BUTTON_GPIO BC_GPIO_P4
#define BLUE_BUTTON_GPIO BC_GPIO_P5
#define PIEZO_BUTTON_GPIO BC_GPIO_P6 // Not used now
#define MAX 20

#define LED_COUNT 204
#define LED_COUNT_PER_POINT ((float)((LED_COUNT - 1.) / MAX))
#define BRIGHTNESS_RED 40
#define BRIGHTNESS_BLUE 40
#define BRIGHTNESS_WHITE_GAP 40

int score_red;
int score_blue;

static bc_led_strip_t led_strip;
static bc_button_t button_red;
static bc_button_t button_blue;
static bc_button_t button_reset_red;
static bc_button_t button_reset_blue;

static bool effect_in_progress;
bc_tick_t sensor_module_task_delay = 100;
bc_scheduler_task_id_t reset_task_id;

// Create costume led strip buffer
static uint32_t _dma_buffer_rgb_204[LED_COUNT * sizeof(uint32_t) * 2];

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

void piezo()
{
    volatile unsigned int i, j;

    for (i = 0; i < 100; i++)
    {
        for (j = 0; j < 400; j++)
            ;
        bc_gpio_set_output(BC_GPIO_P5, true);
        for (j = 0; j < 400; j++)
            ;
        bc_gpio_set_output(BC_GPIO_P5, false);
    }
}

void update_led_strip()
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

    for (i = 0; i <= (LED_COUNT / LED_COUNT_PER_POINT); i++)
    {
        frame_buffer[(unsigned int) (i * LED_COUNT_PER_POINT)].red = 0;
        frame_buffer[(unsigned int) (i * LED_COUNT_PER_POINT)].green = 0;
        frame_buffer[(unsigned int) (i * LED_COUNT_PER_POINT)].blue = 0;
        frame_buffer[(unsigned int) (i * LED_COUNT_PER_POINT)].white = BRIGHTNESS_WHITE_GAP;
    }

    bc_led_strip_set_rgbw_framebuffer(&led_strip, (uint8_t *) frame_buffer, LED_COUNT * 4);

    bc_led_strip_write(&led_strip);
}

void reset_game()
{
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
            piezo();

            if (*score >= MAX)
            {
                effect_in_progress = true;
                bc_led_strip_effect_theater_chase(&led_strip, self == &button_blue ? 0x8000 : 0x80000000, 100);
                bc_led_strip_write(&led_strip);
                bc_scheduler_plan_relative(reset_task_id, 3000);
            }
            else
            {
                (*score)++;
            }
        }

        if (event == BC_BUTTON_EVENT_HOLD)
        {
            piezo();

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
        piezo();

        reset_game();
    }
}

void application_init(void)
{
    // Initialize
    bc_tca9534a_t expander;
    bc_tca9534a_init(&expander, BC_I2C_I2C0, 0x3e);
    bc_tca9534a_set_port_direction(&expander, 0);
    bc_tca9534a_write_port(&expander, 0x60);

    // Initialize power module with led strip
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

    // Initialize piezo gpio pin
    bc_gpio_init(PIEZO_BUTTON_GPIO);
    bc_gpio_set_mode(PIEZO_BUTTON_GPIO, BC_GPIO_MODE_OUTPUT);

    reset_task_id = bc_scheduler_register(reset_game, NULL, BC_TICK_INFINITY);

    reset_game();
}
