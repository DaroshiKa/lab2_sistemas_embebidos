#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

#define NUM_FILAS 6
#define NUM_COLS 6

#define COLOR_NONE   0
#define COLOR_GREEN  1
#define COLOR_RED    2
#define COLOR_ORANGE 3

static const int filas[NUM_FILAS] = {33, 32, 23, 22, 21, 0};
static const int colV[NUM_COLS]   = {13, 14, 26, 19, 5, 16};
static const int colR[NUM_COLS]   = {12, 27, 25, 18, 17, 4};

static uint8_t frame[NUM_FILAS][NUM_COLS];

static inline void fill_frame(uint8_t color) {
    for (int y = 0; y < NUM_FILAS; y++) {
        for (int x = 0; x < NUM_COLS; x++) {
            frame[y][x] = color;
        }
    }
}

static void refresh_display() {
    for (int f = 0; f < NUM_FILAS; f++) {
        for (int i = 0; i < NUM_FILAS; i++) gpio_set_level(filas[i], 0);
        for (int i = 0; i < NUM_COLS; i++) {
            gpio_set_level(colV[i], 0);
            gpio_set_level(colR[i], 0);
        }

        gpio_set_level(filas[f], 1);

        for (int c = 0; c < NUM_COLS; c++) {
            uint8_t color = frame[f][c];

            if (color == COLOR_GREEN || color == COLOR_ORANGE)
                gpio_set_level(colV[c], 1);

            if (color == COLOR_RED || color == COLOR_ORANGE)
                gpio_set_level(colR[c], 1);
        }

        esp_rom_delay_us(400);
    }
}

static void configure_gpio() {
    gpio_config_t out = {
        .pin_bit_mask = 0,
        .mode = GPIO_MODE_OUTPUT
    };

    for (int i = 0; i < NUM_FILAS; i++)
        out.pin_bit_mask |= (1ULL << filas[i]);

    for (int i = 0; i < NUM_COLS; i++) {
        out.pin_bit_mask |= (1ULL << colV[i]);
        out.pin_bit_mask |= (1ULL << colR[i]);
    }

    gpio_config(&out);
}

void app_main() {
    configure_gpio();

    int state = 0;
    uint64_t last_change = esp_timer_get_time();

    while (1) {
        uint64_t now = esp_timer_get_time();

        if (state == 0) {
            fill_frame(COLOR_RED);
        } else if (state == 1) {
            fill_frame(COLOR_GREEN);
        } else {
            fill_frame(COLOR_ORANGE);
        }

        if ((now - last_change) >= 3000000ULL) {
            state++;
            if (state > 2) state = 0;
            last_change = now;
        }

        for (int i = 0; i < 50; i++) {
            refresh_display();
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}