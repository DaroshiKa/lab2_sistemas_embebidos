#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_random.h"
#include "esp_timer.h"

#define NUM_FILAS 6
#define NUM_COLS 6
#define MAX_BALLS 6
#define MAX_POWERUPS 3

#define BTN_IZQ 2
#define BTN_DER 15

#define DEBOUNCE_MS 180

#define COLOR_NONE   0
#define COLOR_GREEN  1
#define COLOR_RED    2
#define COLOR_ORANGE 3

#define STATE_START     0
#define STATE_PLAYING   1
#define STATE_GAME_OVER 2

static const int filas[NUM_FILAS] = {33, 32, 23, 22, 21, 0};
static const int colV[NUM_COLS]   = {13, 14, 26, 19, 5, 16};
static const int colR[NUM_COLS]   = {12, 27, 25, 18, 17, 4};

static int move_interval_ms = 280;
static const int min_move_interval_ms = 80;

typedef struct {
    int x;
    int y;
    int dx;
    int dy;
    bool active;
} Ball;

typedef struct {
    int x;
    int y;
    bool active;
    int blink;
} PowerUp;

static uint8_t frame[NUM_FILAS][NUM_COLS];
static Ball balls[MAX_BALLS];
static PowerUp powerups[MAX_POWERUPS];

static volatile bool btn_izq_flag = false;
static volatile bool btn_der_flag = false;
static volatile TickType_t last_btn_izq_tick = 0;
static volatile TickType_t last_btn_der_tick = 0;

static int game_state = STATE_START;
static int paddle_x = 2;

static int powerup_spawn_ticks = 0;
static int powerup_spawn_delay = 10;

static uint64_t last_update = 0;

static int start_letter = 0;
static int start_offset = -6;
static int hold_counter = 0;
static bool showing_face = false;
static uint64_t start_timer = 0;

static const uint8_t LETTER_S[6][5] = {
    {0,1,1,1,0},
    {1,0,0,0,0},
    {0,1,1,1,0},
    {0,0,0,0,1},
    {1,1,1,1,0},
    {0,0,0,0,0}
};

static const uint8_t LETTER_T[6][5] = {
    {1,1,1,1,1},
    {0,0,1,0,0},
    {0,0,1,0,0},
    {0,0,1,0,0},
    {0,0,1,0,0},
    {0,0,0,0,0}
};

static const uint8_t LETTER_A[6][5] = {
    {0,1,1,1,0},
    {1,0,0,0,1},
    {1,1,1,1,1},
    {1,0,0,0,1},
    {1,0,0,0,1},
    {0,0,0,0,0}
};

static const uint8_t LETTER_R[6][5] = {
    {1,1,1,1,0},
    {1,0,0,0,1},
    {1,1,1,1,0},
    {1,0,1,0,0},
    {1,0,0,1,0},
    {0,0,0,0,0}
};

void IRAM_ATTR isr_btn_izq(void* arg) {
    TickType_t now = xTaskGetTickCountFromISR();
    if ((now - last_btn_izq_tick) > pdMS_TO_TICKS(DEBOUNCE_MS)) {
        btn_izq_flag = true;
        last_btn_izq_tick = now;
    }
}

void IRAM_ATTR isr_btn_der(void* arg) {
    TickType_t now = xTaskGetTickCountFromISR();
    if ((now - last_btn_der_tick) > pdMS_TO_TICKS(DEBOUNCE_MS)) {
        btn_der_flag = true;
        last_btn_der_tick = now;
    }
}

static inline void clear_frame() {
    memset(frame, COLOR_NONE, sizeof(frame));
}

static inline void fill_frame(uint8_t c) {
    memset(frame, c, sizeof(frame));
}

static inline void put_pixel(int x, int y, uint8_t color) {
    if (x < 0 || x >= NUM_COLS || y < 0 || y >= NUM_FILAS) return;
    frame[y][x] = color;
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

            gpio_set_level(colV[c], 0);
            gpio_set_level(colR[c], 0);

            if (color == COLOR_GREEN || color == COLOR_ORANGE)
                gpio_set_level(colV[c], 1);

            if (color == COLOR_RED || color == COLOR_ORANGE)
                gpio_set_level(colR[c], 1);
        }

        esp_rom_delay_us(400);
    }
}

static void draw_letter_offset(const uint8_t letter[6][5], int offset) {
    fill_frame(COLOR_GREEN);

    for (int y = 0; y < 6; y++) {
        for (int x = 0; x < 5; x++) {
            if (letter[y][x]) {
                int px = 4 - x;
                int py = 5 - y + offset;
                if (px >= 0 && px < 6 && py >= 0 && py < 6)
                    put_pixel(px, py, COLOR_RED);
            }
        }
    }
}

static void draw_tongue_face() {
    fill_frame(COLOR_NONE);

    put_pixel(0, 5, COLOR_RED);
    put_pixel(1, 4, COLOR_RED);
    put_pixel(4, 4, COLOR_RED);
    put_pixel(5, 5, COLOR_RED);

    put_pixel(1, 3, COLOR_GREEN);
    put_pixel(1, 2, COLOR_GREEN);

    put_pixel(4, 3, COLOR_GREEN);
    put_pixel(4, 2, COLOR_GREEN);

    put_pixel(1, 1, COLOR_RED);
    put_pixel(2, 0, COLOR_RED);
    put_pixel(3, 0, COLOR_RED);
    put_pixel(4, 1, COLOR_RED);

    put_pixel(2, 1, COLOR_ORANGE);
    put_pixel(3, 1, COLOR_ORANGE);
    put_pixel(2, 2, COLOR_ORANGE);
    put_pixel(3, 2, COLOR_ORANGE);
}

static void render_start() {
    uint64_t now = esp_timer_get_time();

    if (now - start_timer > 60000) {
        start_timer = now;

        if (showing_face) {
            hold_counter++;
            if (hold_counter > 12) {
                showing_face = false;
                hold_counter = 0;
                start_letter = 0;
                start_offset = -6;
            }
            return;
        }

        if (start_offset < 0) {
            start_offset++;
        } else {
            hold_counter++;
            if (hold_counter > 7) {
                hold_counter = 0;
                start_offset = -6;
                start_letter++;

                if (start_letter > 4) {
                    showing_face = true;
                }
            }
        }
    }

    if (showing_face) {
        draw_tongue_face();
        return;
    }

    switch (start_letter) {
        case 0: draw_letter_offset(LETTER_S, start_offset); break;
        case 1: draw_letter_offset(LETTER_T, start_offset); break;
        case 2: draw_letter_offset(LETTER_A, start_offset); break;
        case 3: draw_letter_offset(LETTER_R, start_offset); break;
        case 4: draw_letter_offset(LETTER_T, start_offset); break;
    }
}

static void draw_sad_face() {
    fill_frame(COLOR_NONE);

    put_pixel(1,4,COLOR_RED);
    put_pixel(1,3,COLOR_RED);
    put_pixel(4,4,COLOR_RED);
    put_pixel(4,3,COLOR_RED);

    put_pixel(1,1,COLOR_RED);
    put_pixel(2,0,COLOR_RED);
    put_pixel(3,0,COLOR_RED);
    put_pixel(4,1,COLOR_RED);
}

static void reset_balls() {
    memset(balls, 0, sizeof(balls));
    balls[0] = (Ball){2,2,1,1,true};
}

static void reset_powerups() {
    memset(powerups, 0, sizeof(powerups));
    powerup_spawn_ticks = 0;
    powerup_spawn_delay = 8 + (esp_random() % 8);
}

static void init_game() {
    reset_balls();
    reset_powerups();
    paddle_x = 2;
    move_interval_ms = 280;
    last_update = esp_timer_get_time();
}

static void draw_paddle() {
    for (int i = 0; i < 2; i++)
        put_pixel(paddle_x + i, 0, COLOR_RED);
}

static void draw_balls() {
    for (int i = 0; i < MAX_BALLS; i++)
        if (balls[i].active)
            put_pixel(balls[i].x, balls[i].y, COLOR_GREEN);
}

static void draw_powerups() {
    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (!powerups[i].active) continue;
        if ((powerups[i].blink / 4) % 2 == 0)
            put_pixel(powerups[i].x, powerups[i].y, COLOR_ORANGE);
    }
}

static void render_game() {
    clear_frame();
    draw_paddle();
    draw_balls();
    draw_powerups();
}

static void duplicate_ball(int i) {
    for (int j = 0; j < MAX_BALLS; j++) {
        if (!balls[j].active) {
            balls[j] = balls[i];
            balls[j].dx = -balls[i].dx;
            balls[j].dy = (esp_random() % 2) ? 1 : -1;
            balls[j].active = true;
            return;
        }
    }
}

static int active_powerups() {
    int n = 0;
    for (int i = 0; i < MAX_POWERUPS; i++)
        if (powerups[i].active) n++;
    return n;
}

static void spawn_powerup() {
    if (active_powerups() >= MAX_POWERUPS) return;

    for (int tries = 0; tries < 10; tries++) {
        int x = esp_random() % NUM_COLS;
        int y = 2 + esp_random() % (NUM_FILAS - 2);
        bool occupied = false;

        for (int i = 0; i < MAX_POWERUPS; i++) {
            if (powerups[i].active && powerups[i].x == x && powerups[i].y == y) {
                occupied = true;
                break;
            }
        }

        if (!occupied) {
            for (int i = 0; i < MAX_POWERUPS; i++) {
                if (!powerups[i].active) {
                    powerups[i].active = true;
                    powerups[i].x = x;
                    powerups[i].y = y;
                    powerups[i].blink = 0;
                    return;
                }
            }
        }
    }
}

static void update_powerup_spawns() {
    powerup_spawn_ticks++;

    if (powerup_spawn_ticks >= powerup_spawn_delay) {
        spawn_powerup();
        powerup_spawn_ticks = 0;
        powerup_spawn_delay = 8 + (esp_random() % 10);
    }
}

static void ball_collisions() {
    for (int i = 0; i < MAX_BALLS; i++) {
        if (!balls[i].active) continue;
        for (int j = i + 1; j < MAX_BALLS; j++) {
            if (!balls[j].active) continue;

            if (balls[i].x == balls[j].x && balls[i].y == balls[j].y) {
                int tdx = balls[i].dx;
                int tdy = balls[i].dy;
                balls[i].dx = balls[j].dx;
                balls[i].dy = balls[j].dy;
                balls[j].dx = tdx;
                balls[j].dy = tdy;
            }
        }
    }
}

static void update_ball(int i) {
    if (!balls[i].active) return;

    int nx = balls[i].x + balls[i].dx;
    int ny = balls[i].y + balls[i].dy;

    if (nx < 0 || nx >= NUM_COLS) {
        balls[i].dx *= -1;
        nx = balls[i].x + balls[i].dx;
    }

    if (ny >= NUM_FILAS) {
        balls[i].dy *= -1;
        ny = balls[i].y + balls[i].dy;
    }

    if (ny <= 0) {
        if (nx >= paddle_x && nx <= paddle_x + 1) {
            balls[i].dy = 1;

            if (nx == paddle_x) {
                balls[i].dx = -1;
            } else if (nx == paddle_x + 1) {
                balls[i].dx = 1;
            }

            if ((esp_random() % 100) < 25) {
                balls[i].dx *= -1;
            }

            if (move_interval_ms > min_move_interval_ms) {
                move_interval_ms -= 8;
                if (move_interval_ms < min_move_interval_ms)
                    move_interval_ms = min_move_interval_ms;
            }
        } else {
            balls[i].active = false;
        }
        return;
    }

    balls[i].x = nx;
    balls[i].y = ny;

    for (int p = 0; p < MAX_POWERUPS; p++) {
        if (!powerups[p].active) continue;
        if (balls[i].x == powerups[p].x && balls[i].y == powerups[p].y) {
            powerups[p].active = false;
            duplicate_ball(i);
            break;
        }
    }
}

static int active_balls() {
    int n = 0;
    for (int i = 0; i < MAX_BALLS; i++)
        if (balls[i].active) n++;
    return n;
}

static void update_game() {
    uint64_t now = esp_timer_get_time();

    if ((now - last_update) < (uint64_t)move_interval_ms * 1000ULL) {
        for (int i = 0; i < MAX_POWERUPS; i++)
            if (powerups[i].active) powerups[i].blink++;
        return;
    }

    last_update = now;

    if (btn_izq_flag) {
        btn_izq_flag = false;
        if (paddle_x > 0) paddle_x--;
    }

    if (btn_der_flag) {
        btn_der_flag = false;
        if (paddle_x < NUM_COLS - 2) paddle_x++;
    }

    update_powerup_spawns();

    for (int i = 0; i < MAX_BALLS; i++)
        update_ball(i);

    ball_collisions();

    for (int i = 0; i < MAX_POWERUPS; i++)
        if (powerups[i].active) powerups[i].blink++;

    if (active_balls() == 0)
        game_state = STATE_GAME_OVER;
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

    gpio_config_t in = {
        .pin_bit_mask = (1ULL << BTN_IZQ) | (1ULL << BTN_DER),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };

    gpio_config(&in);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(BTN_IZQ, isr_btn_izq, NULL);
    gpio_isr_handler_add(BTN_DER, isr_btn_der, NULL);
}

void app_main() {
    configure_gpio();

    while (1) {
        if (game_state == STATE_START) {
            render_start();

            if (btn_izq_flag || btn_der_flag) {
                btn_izq_flag = false;
                btn_der_flag = false;
                init_game();
                game_state = STATE_PLAYING;
            }
        } else if (game_state == STATE_PLAYING) {
            update_game();
            render_game();
        } else {
            draw_sad_face();

            if (btn_izq_flag || btn_der_flag) {
                btn_izq_flag = false;
                btn_der_flag = false;
                game_state = STATE_START;
                start_letter = 0;
                start_offset = -6;
                hold_counter = 0;
                showing_face = false;
                start_timer = esp_timer_get_time();
            }
        }

        for (int i = 0; i < 40; i++)
            refresh_display();

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}