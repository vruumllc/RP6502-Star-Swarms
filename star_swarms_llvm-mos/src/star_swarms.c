// ---------------------------------------------------------------------------
// star_swarms.c
//
// I don't care what you do with this code -- except have fun!
//
// tonyvr
// ---------------------------------------------------------------------------

#include <rp6502.h>
#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include "usb_hid_keys.h"
#include "bitmap_graphics.h"

#define CANVAS_W 320
#define CANVAS_H 240

// XRAM locations
#define KEYBORD_INPUT 0xFF10 // KEYBOARD_BYTES of bitmask data

// 256 bytes HID code max, stored in 32 uint8
#define KEYBOARD_BYTES 32
uint8_t keystates[KEYBOARD_BYTES] = {0};

// keystates[code>>3] gets contents from correct byte in array
// 1 << (code&7) moves a 1 into proper position to mask with byte contents
// final & gives 1 if key is pressed, 0 if not
#define key(code) (keystates[code >> 3] & (1 << (code & 7)))

#define MOVEMENT_TIMER_THRESHOLD_MAX 10
#define ATTACK_TIMER_THRESHOLD_MAX 480 // 8 seconds
#define EXPLOSION_TIMER_THRESHOLD_MAX 30 // 1/2 second

#define TILE_CONFIG_SIZE sizeof(vga_mode2_config_t)
#define TILE_CONFIG 0xE600 // (actually 0x1E500)
#define TILE_IMAGE_DATA 0xB000 // (actually 0x1B000)
#define TILE_MAP_DATA 0xE700 // (actually 0x1E700)
#define TILE_SIZE 16

// ----------------------------------------------------------------------------
// All fix_8 tables assume 15 degrees incr. for each index, 0 to 360 degrees.
// ----------------------------------------------------------------------------
typedef int16_t fix8_t;
#define FIX8(a) (fix8_t)((a)<<8)

static const int16_t sin_fix8[] = {
      0,  66, 128, 181, 222, 247, 256, 247, 222, 181, 128,  66,
      0, -66,-128,-181,-222,-247,-256,-247,-222,-181,-128, -66,
      0
};

static const int16_t cos_fix8[] = {
    256, 247, 222, 181, 128,  66,   0, -66,-128,-181,-222,-247,
   -256,-247,-222,-181,-120, -66,   0,  66, 128, 181, 222, 247,
    256
};

// fix_8((1+(sin(theta_deg)-cos(theta_deg)))/2)
static const int16_t t2_fix8[] = {
      0,  37,  81, 128, 175, 219, 256, 285, 303, 309, 303, 285,
    256, 219, 175, 128,  81,  37,   0, -29, -47, -53, -47, -29,
      0
};

// fix_8((1+(sin(360-theta_deg)-cos(360-theta_deg)))/2)
// (just the reverse of table above, so unnecessary!!
/*
int16_t t5_fix8[] = {
      0, -29, -47, -53, -47, -29,   0,  37,  81, 128, 175, 219,
    256, 285, 303, 309, 303, 285, 256, 219, 175, 128,  81,  37,
      0
};
*/
// ----------------------------------------------------------------------------

#define EXPLOSION_SIZE 32
#define LOG_EXPLOSION_SIZE 5 // 2^5 = 32
#define SHIP_SIZE 16
#define HALF_SHIP_SIZE 8
#define LOG_SHIP_SIZE 4 // 2^4 = 16
#define SHIP_SPACING_X 22
#define SHIP_SPACING_Y 16
#define MISSILE_W 2
#define HALF_MISSILE_W 1
#define MISSILE_H 4
#define HALF_MISSILE_H 2
#define LOG_MISSILE_SIZE 2 // 2^2 = 4

#define SPACESHIP_DATA 0xB800 // (actually 0x1B800)
#define ALIEN_BLUE_DATA 0xBA00
#define ALIEN_GREEN_DATA 0xBC00
#define ALIEN_YELLOW_DATA 0xBE00
#define ALIEN_PINK_DATA 0xC000
#define ALIEN_RED_DATA 0xC200
#define ALIEN_WHITE_DATA 0xC400
#define ALIEN_MISSILE_DATA 0xC600
#define MISSILE_DATA 0xC620
#define EXPLOSION_DATA 0xC640
// total extended memory consumed by data =  0x1400

#define EXPLOSION_NUM 1
#define SPACESHIP_NUM 1
#define MISSILE_NUM 1
#define ALIEN_BLUE_NUM 10
#define ALIEN_GREEN_NUM 10
#define ALIEN_YELLOW_NUM 10
#define ALIEN_PINK_NUM 8
#define ALIEN_RED_NUM 6
#define ALIEN_WHITE_NUM 2
#define ALIEN_MISSILE_NUM 10
#define NUM_ALIENS (ALIEN_BLUE_NUM + ALIEN_GREEN_NUM + ALIEN_YELLOW_NUM +\
                    ALIEN_PINK_NUM + ALIEN_RED_NUM   + ALIEN_WHITE_NUM)

#define SPACESHIP_OFFSET 0
#define MISSILE_OFFSET (SPACESHIP_OFFSET + SPACESHIP_NUM)
#define ALIEN_BLUE_OFFSET (MISSILE_OFFSET + MISSILE_NUM)
#define ALIEN_GREEN_OFFSET (ALIEN_BLUE_OFFSET + ALIEN_BLUE_NUM)
#define ALIEN_YELLOW_OFFSET (ALIEN_GREEN_OFFSET + ALIEN_GREEN_NUM)
#define ALIEN_PINK_OFFSET (ALIEN_YELLOW_OFFSET + ALIEN_YELLOW_NUM)
#define ALIEN_RED_OFFSET (ALIEN_PINK_OFFSET + ALIEN_PINK_NUM)
#define ALIEN_WHITE_OFFSET (ALIEN_RED_OFFSET + ALIEN_RED_NUM)
#define ALIEN_MISSILE_OFFSET (ALIEN_WHITE_OFFSET + ALIEN_WHITE_NUM)
#define EXPLOSION_OFFSET (ALIEN_MISSILE_OFFSET + ALIEN_MISSILE_NUM)
#define TOTAL_ASPRITES (EXPLOSION_OFFSET + EXPLOSION_NUM)

#define ASPRITE_CONFIG_SIZE sizeof(vga_mode4_asprite_t)
#define ASPRITE_CONFIGS 0xE000 // start of affine sprite configs (actually 0x1E000)
// total extended memory consumed by configs = TOTAL_ASPRITES * ASPRITE_CONFIG_SIZE = 0x0488

typedef enum {MOVING_LEFT, FWD_AT_LEFT, MOVING_RIGHT, FWD_AT_RIGHT} alien_move;
typedef enum {MISSILE_TYPE, SHIP_TYPE, EXPLOSION_TYPE} asprite_type;
typedef enum {HIDDEN, CRUISING, LAUNCHING, MOVING, PARKING} asprite_state;
typedef struct {
    int16_t x_pos;
    int16_t y_pos;
    int16_t theta;
    uint16_t config_ptr;
    uint16_t data_ptr;
    asprite_type type;
    asprite_state state;
    uint8_t attack_step;
    uint8_t row;
    uint8_t col;
} asprite_t;
static asprite_t asprite[TOTAL_ASPRITES];

// init static const indexes here to avoid math every substitution
static const uint8_t SPACESHIP_INDEX = SPACESHIP_OFFSET;
static const uint8_t MISSILE_INDEX = MISSILE_OFFSET;
static const uint8_t ALIEN_BLUE_INDEX = ALIEN_BLUE_OFFSET;
static const uint8_t ALIEN_GREEN_INDEX = ALIEN_GREEN_OFFSET;
static const uint8_t ALIEN_YELLOW_INDEX = ALIEN_YELLOW_OFFSET;
static const uint8_t ALIEN_PINK_INDEX = ALIEN_PINK_OFFSET;
static const uint8_t ALIEN_RED_INDEX = ALIEN_RED_OFFSET;
static const uint8_t ALIEN_WHITE_INDEX = ALIEN_WHITE_OFFSET;
static const uint8_t ALIEN_MISSILE_INDEX = ALIEN_MISSILE_OFFSET;
static const uint8_t EXPLOSION_INDEX = EXPLOSION_OFFSET;
static const uint8_t NUM_ASPRITES = TOTAL_ASPRITES;

// where to draw stuff
#define ALIEN_BLUE_X  ((CANVAS_W-SHIP_SIZE-(ALIEN_BLUE_NUM-1)*SHIP_SPACING_X)/2)
#define ALIEN_GREEN_X ((CANVAS_W-SHIP_SIZE-(ALIEN_GREEN_NUM-1)*SHIP_SPACING_X)/2)
#define ALIEN_YELLOW_X ((CANVAS_W-SHIP_SIZE-(ALIEN_YELLOW_NUM-1)*SHIP_SPACING_X)/2)
#define ALIEN_PINK_X ((CANVAS_W-SHIP_SIZE-(ALIEN_PINK_NUM-1)*SHIP_SPACING_X)/2)
#define ALIEN_RED_X ((CANVAS_W-SHIP_SIZE-(ALIEN_RED_NUM-1)*SHIP_SPACING_X)/2)
#define ALIEN_WHITE_X ((CANVAS_W-SHIP_SIZE-3*SHIP_SPACING_X)/2)

static const uint16_t shields_x = (1*CANVAS_W/4);
static const uint16_t shields_y = 0;
static const uint16_t swarm_x = (2*CANVAS_W/4);
static const uint16_t swarm_y = 0;
static const uint16_t score_x = (3*CANVAS_W/4);
static const uint16_t score_y = 0;
static uint16_t alien_blue_x = ALIEN_BLUE_X;
static const uint16_t alien_blue_y = (CANVAS_H-SHIP_SIZE)/2-1*SHIP_SPACING_Y;
static uint16_t alien_green_x = ALIEN_GREEN_X;
static const uint16_t alien_green_y = (CANVAS_H-SHIP_SIZE)/2-2*SHIP_SPACING_Y;
static uint16_t alien_yellow_x = ALIEN_YELLOW_X;
static const uint16_t alien_yellow_y = (CANVAS_H-SHIP_SIZE)/2-3*SHIP_SPACING_Y;
static uint16_t alien_pink_x = ALIEN_PINK_X;
static const uint16_t alien_pink_y = (CANVAS_H-SHIP_SIZE)/2-4*SHIP_SPACING_Y;
static uint16_t alien_red_x = ALIEN_RED_X;
static const uint16_t alien_red_y = (CANVAS_H-SHIP_SIZE)/2-5*SHIP_SPACING_Y;
static uint16_t alien_white_x = ALIEN_WHITE_X;
static const uint16_t alien_white_y = (CANVAS_H-SHIP_SIZE)/2-6*SHIP_SPACING_Y;

static const int16_t hit_threshold = (SHIP_SIZE-2)/2;

static int8_t current_shift = 0;
static uint16_t current_swarm = 1;
static int16_t current_score = 0;
static uint8_t current_shields = 100;
static uint16_t movement_timer_threshold = MOVEMENT_TIMER_THRESHOLD_MAX;
static uint16_t attack_timer_threshold = ATTACK_TIMER_THRESHOLD_MAX;
static uint16_t explosion_timer_threshold = EXPLOSION_TIMER_THRESHOLD_MAX;
static uint16_t explosion_timer = 0;

static bool paused = false;
static bool game_over = false;

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
static void update_shields()
{
    char str_shields[10] = {0};
    set_text_multiplier(1);
    set_text_color(CYAN);
    set_cursor(shields_x+52, shields_y);
    fill_rect(BLACK, shields_x+52, shields_y, 4*6, 8);
    snprintf(str_shields, 10, "%d%%", current_shields);
    draw_string(str_shields);
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
static void update_swarm()
{
    char str_swarm[10] = {0};
    set_text_multiplier(1);
    set_text_color(CYAN);
    set_cursor(swarm_x+45, swarm_y);
    fill_rect(BLACK, swarm_x+45, swarm_y, 4*6, 8);
    snprintf(str_swarm, 10, "%u", current_swarm);
    draw_string(str_swarm);
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
static void update_score()
{
    char str_score[10] = {0};
    set_text_multiplier(1);
    set_text_color(CYAN);
    set_cursor(score_x+45, score_y);
    fill_rect(BLACK, score_x+45, score_y, 4*6, 8);
    snprintf(str_score, 10, "%d", current_score);
    draw_string(str_score);
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
static void update_paused()
{
    if (paused) {
        set_text_multiplier(1);
        set_text_colors(YELLOW, DARK_RED);
        set_cursor(0, canvas_height()-8);
        if (game_over) {
            draw_string(" !! GAME OVER !! ");
        } else {
            draw_string(" !!! PAUSED !!!  ");
        }
    } else {
        fill_rect(BLACK, 0, canvas_height()-8, 17*6, 8);
    }
}

// ----------------------------------------------------------------------------
// Draw the static text elements (only once).
// ----------------------------------------------------------------------------
static void draw_text()
{
    // draw title
    set_text_multiplier(1);
    set_text_colors(YELLOW, DARK_RED);
    set_cursor(0, 0);
    draw_string("STAR SWARMS");

    // draw shields text
    set_text_multiplier(1);
    set_text_color(WHITE);
    set_cursor(shields_x, shields_y);
    draw_string("Shields:");
    update_shields();

    // draw swarm text
    set_text_multiplier(1);
    set_text_color(WHITE);
    set_cursor(swarm_x, swarm_y);
    draw_string("Swarm:");
    update_swarm();

    // draw score text
    set_text_multiplier(1);
    set_text_color(WHITE);
    set_cursor(score_x, score_y);
    draw_string("Score:");
    update_score();
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
void init_the_universe()
{
    uint16_t i;

    xram0_struct_set(TILE_CONFIG, vga_mode2_config_t, x_wrap, false);
    xram0_struct_set(TILE_CONFIG, vga_mode2_config_t, y_wrap, true);
    xram0_struct_set(TILE_CONFIG, vga_mode2_config_t, x_pos_px, 0);
    xram0_struct_set(TILE_CONFIG, vga_mode2_config_t, y_pos_px, 0);
    xram0_struct_set(TILE_CONFIG, vga_mode2_config_t, width_tiles, 40); // bug? 20 only draws 10!
    xram0_struct_set(TILE_CONFIG, vga_mode2_config_t, height_tiles, 15);
    xram0_struct_set(TILE_CONFIG, vga_mode2_config_t, xram_data_ptr, TILE_MAP_DATA);
    xram0_struct_set(TILE_CONFIG, vga_mode2_config_t, xram_palette_ptr, 0xFFFF);
    xram0_struct_set(TILE_CONFIG, vga_mode2_config_t, xram_tile_ptr, TILE_IMAGE_DATA);

    // 16x16 tiles w/ bpp4 color
    //xreg_vga_mode(2, 10, TILE_CONFIG, 0, TILE_SIZE, 15*TILE_SIZE-1);
    xregn(1, 0, 1, 6, 2, 10, TILE_CONFIG, 0, TILE_SIZE, 15*TILE_SIZE-1);

    // init the tile map
    RIA.addr0 = TILE_MAP_DATA;
    RIA.step0 = 1;
    for (i = 0; i < 40 * 15; i++) {
        uint8_t n = 15 - lrand()%16;
        // add more dark tiles
        if (lrand()%2 == 0) {
            RIA.rw0 = 0;
        } else {
            RIA.rw0 = n;
        }
    }
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
void scroll_stars()
{
    static uint8_t y = 0;

    xram0_struct_set(TILE_CONFIG, vga_mode2_config_t, y_pos_px, y);
    if (++y >= CANVAS_H) {
        y = 0;
    }
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
void move_asprite(uint8_t i, int16_t x, int16_t y)
{
    uint16_t ptr = asprite[i].config_ptr; // config address

    // update X
    RIA.addr0 = ptr + 12;
    RIA.step0 = ASPRITE_CONFIG_SIZE;
    RIA.addr1 = ptr + 13;
    RIA.step1 = ASPRITE_CONFIG_SIZE;

    RIA.rw0 = x & 0xff;
    RIA.rw1 = (x >> 8) & 0xff;

    // update Y
    RIA.addr0 = ptr + 14;
    RIA.addr1 = ptr + 15;

    RIA.rw0 = y & 0xff;
    RIA.rw1 = (y >> 8) & 0xff;

    asprite[i].x_pos = x;
    asprite[i].y_pos = y;
}

// ----------------------------------------------------------------------------
// Rotate an affine sprite around its center point, rather than top left.
//
// The asprite_dim is W (and H) in pixels of the affine sprite.
// Positive theta_deg for CW rotation, negative theta_deg for CCW.
//
// NOTE: This routine assumes sprite is NOT scaled!
//       I haven't figured out how to rotate a scaled sprite yet.
// ----------------------------------------------------------------------------
bool rotate_asprite(uint8_t i, uint8_t asprite_dim, int16_t theta_deg)
{
    // the number of elements in each array
    static const uint8_t n = sizeof(sin_fix8) / sizeof(int16_t);
    uint16_t ptr = asprite[i].config_ptr; // config address

    // check theta_deg for validity
    if ((theta_deg >= -360) && (theta_deg <= 360) && ((theta_deg%(360/(n-1))) == 0)) {

        // compute desired index into arrays
        uint8_t j;
        if (theta_deg > 0) { // want clockwise rotation
            j = (360-theta_deg)/(360/(n-1));
        } else {             // want counter-clockwise
            j = -theta_deg/(360/(n-1));
        }
        xram0_struct_set(ptr, vga_mode4_asprite_t, transform[0], cos_fix8[j]);
        xram0_struct_set(ptr, vga_mode4_asprite_t, transform[1], -sin_fix8[j]);
        xram0_struct_set(ptr, vga_mode4_asprite_t, transform[2], asprite_dim*t2_fix8[j]);
        xram0_struct_set(ptr, vga_mode4_asprite_t, transform[3], sin_fix8[j]);
        xram0_struct_set(ptr, vga_mode4_asprite_t, transform[4], cos_fix8[j]);
        xram0_struct_set(ptr, vga_mode4_asprite_t, transform[5], asprite_dim*t2_fix8[n-j-1]);

        asprite[i].theta = theta_deg;
        return true;
    }
    printf("Invalid theta_deg = %d!/n", theta_deg);
    return false;
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
void init_asprite_config(uint8_t i)
{
    uint16_t ptr = asprite[i].config_ptr;
    uint8_t asprite_dim = asprite[i].type == EXPLOSION_TYPE ? EXPLOSION_SIZE :
                         (asprite[i].type == SHIP_TYPE ? SHIP_SIZE : MISSILE_H);
    uint8_t asprite_log_size = asprite[i].type == EXPLOSION_TYPE ? LOG_EXPLOSION_SIZE :
                              (asprite[i].type == SHIP_TYPE ? LOG_SHIP_SIZE : LOG_MISSILE_SIZE);

    rotate_asprite(i, asprite_dim, asprite[i].theta);
    xram0_struct_set(ptr, vga_mode4_asprite_t, x_pos_px, asprite[i].x_pos);
    xram0_struct_set(ptr, vga_mode4_asprite_t, y_pos_px, asprite[i].y_pos);
    xram0_struct_set(ptr, vga_mode4_asprite_t, xram_sprite_ptr, asprite[i].data_ptr);
    xram0_struct_set(ptr, vga_mode4_asprite_t, log_size, asprite_log_size);
    xram0_struct_set(ptr, vga_mode4_asprite_t, has_opacity_metadata, false);
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
void init_asprites()
{
    uint8_t i;    // asprite index
    uint16_t ptr; // config address
    uint8_t n;

    // init spaceship
    i = SPACESHIP_INDEX;
    ptr = ASPRITE_CONFIGS + i*ASPRITE_CONFIG_SIZE;
    asprite[i].x_pos = (CANVAS_W - SHIP_SIZE)/2 + 1;
    asprite[i].y_pos = CANVAS_H - SHIP_SIZE - 1;
    asprite[i].theta = 0;
    asprite[i].config_ptr = ptr;
    asprite[i].data_ptr = SPACESHIP_DATA;
    asprite[i].type = SHIP_TYPE;
    asprite[i].state = CRUISING;
    asprite[i].attack_step = 0;
    asprite[i].row = 0;
    asprite[i].col = 0;
    init_asprite_config(i);

    // init missile
    i = MISSILE_INDEX;
    ptr = ASPRITE_CONFIGS + i*ASPRITE_CONFIG_SIZE;
    asprite[i].x_pos = (CANVAS_W - MISSILE_W)/2;
    asprite[i].y_pos = CANVAS_H - SHIP_SIZE - MISSILE_H - 1;
    asprite[i].theta = 0;
    asprite[i].config_ptr = ptr;
    asprite[i].data_ptr = MISSILE_DATA;
    asprite[i].type = MISSILE_TYPE;
    asprite[i].state = CRUISING;
    asprite[i].attack_step = 0;
    asprite[i].row = 0;
    asprite[i].col = 0;
    init_asprite_config(i);

    // init explosion
    i = EXPLOSION_INDEX;
    ptr = ASPRITE_CONFIGS + i*ASPRITE_CONFIG_SIZE;
    asprite[i].x_pos = CANVAS_W/2 - SHIP_SIZE + 1;
    asprite[i].y_pos = 2*CANVAS_H;
    asprite[i].theta = 0;
    asprite[i].config_ptr = ptr;
    asprite[i].data_ptr = EXPLOSION_DATA;
    asprite[i].type = EXPLOSION_TYPE;
    asprite[i].state = HIDDEN;
    asprite[i].attack_step = 0;
    asprite[i].row = 0;
    asprite[i].col = 0;
    init_asprite_config(i);

    // init blue aliens
    for (n=0; n<ALIEN_BLUE_NUM; n++) {
        i = ALIEN_BLUE_INDEX + n;
        ptr = ASPRITE_CONFIGS + i*ASPRITE_CONFIG_SIZE;
        asprite[i].x_pos = alien_blue_x + n*SHIP_SPACING_X;
        asprite[i].y_pos = alien_blue_y;
        asprite[i].theta = 180;
        asprite[i].config_ptr = ptr;
        asprite[i].data_ptr = ALIEN_BLUE_DATA;
        asprite[i].type = SHIP_TYPE;
        asprite[i].state = CRUISING;
        asprite[i].attack_step = 0;
        asprite[i].row = 0;
        asprite[i].col = n;
        init_asprite_config(i);
    }

    // init green aliens
    for (n=0; n<ALIEN_GREEN_NUM; n++) {
        i = ALIEN_GREEN_INDEX + n;
        ptr = ASPRITE_CONFIGS + i*ASPRITE_CONFIG_SIZE;
        asprite[i].x_pos = alien_green_x + n*SHIP_SPACING_X;
        asprite[i].y_pos = alien_green_y;
        asprite[i].theta = 180;
        asprite[i].config_ptr = ptr;
        asprite[i].data_ptr = ALIEN_GREEN_DATA;
        asprite[i].type = SHIP_TYPE;
        asprite[i].state = CRUISING;
        asprite[i].attack_step = 0;
        asprite[i].row = 1;
        asprite[i].col = n;
        init_asprite_config(i);
    }

    // init yellow aliens
    for (n=0; n<ALIEN_YELLOW_NUM; n++) {
        i = ALIEN_YELLOW_INDEX + n;
        ptr = ASPRITE_CONFIGS + i*ASPRITE_CONFIG_SIZE;
        asprite[i].x_pos = alien_yellow_x + n*SHIP_SPACING_X;;
        asprite[i].y_pos = alien_yellow_y;
        asprite[i].theta = 180;
        asprite[i].config_ptr = ptr;
        asprite[i].data_ptr = ALIEN_YELLOW_DATA;
        asprite[i].type = SHIP_TYPE;
        asprite[i].state = CRUISING;
        asprite[i].attack_step = 0;
        asprite[i].row = 2;
        asprite[i].col = n;
        init_asprite_config(i);
    }

    // init pink aliens
    for (n=0; n<ALIEN_PINK_NUM; n++) {
        i = ALIEN_PINK_INDEX + n;
        ptr = ASPRITE_CONFIGS + i*ASPRITE_CONFIG_SIZE;
        asprite[i].x_pos = alien_pink_x + n*SHIP_SPACING_X;;
        asprite[i].y_pos = alien_pink_y;
        asprite[i].theta = 180;
        asprite[i].config_ptr = ptr;
        asprite[i].data_ptr = ALIEN_PINK_DATA;
        asprite[i].type = SHIP_TYPE;
        asprite[i].state = CRUISING;
        asprite[i].attack_step = 0;
        asprite[i].row = 3;
        asprite[i].col = 1+n;
        init_asprite_config(i);
    }

    // init red aliens
    for (n=0; n<ALIEN_RED_NUM; n++) {
        i = ALIEN_RED_INDEX + n;
        ptr = ASPRITE_CONFIGS + i*ASPRITE_CONFIG_SIZE;
        asprite[i].x_pos = alien_red_x + n*SHIP_SPACING_X;;
        asprite[i].y_pos = alien_red_y;
        asprite[i].theta = 180;
        asprite[i].config_ptr = ptr;
        asprite[i].data_ptr = ALIEN_RED_DATA;
        asprite[i].type = SHIP_TYPE;
        asprite[i].state = CRUISING;
        asprite[i].attack_step = 0;
        asprite[i].row = 4;
        asprite[i].col = 2+n;
        init_asprite_config(i);
    }

    // init white aliens
    for (n=0; n<ALIEN_WHITE_NUM; n++) {
        i = ALIEN_WHITE_INDEX + n;
        ptr = ASPRITE_CONFIGS + i*ASPRITE_CONFIG_SIZE;
        asprite[i].x_pos = (n == 0) ? alien_white_x : (alien_white_x + 3*SHIP_SPACING_X);
        asprite[i].y_pos = alien_white_y;
        asprite[i].theta = 180;
        asprite[i].config_ptr = ptr;
        asprite[i].data_ptr = ALIEN_WHITE_DATA;
        asprite[i].type = SHIP_TYPE;
        asprite[i].state = CRUISING;
        asprite[i].attack_step = 0;
        asprite[i].row = 5;
        asprite[i].col = ((n == 0) ? 3 : 6);
        init_asprite_config(i);
    }

    // init alien missiles
    for (n=0; n<ALIEN_MISSILE_NUM; n++) {
        i = ALIEN_MISSILE_INDEX + n;
        ptr = ASPRITE_CONFIGS + i*ASPRITE_CONFIG_SIZE;
        asprite[i].x_pos = CANVAS_W + 10;
        asprite[i].y_pos = CANVAS_H + 10;
        asprite[i].theta = 0;
        asprite[i].config_ptr = ptr;
        asprite[i].data_ptr = ALIEN_MISSILE_DATA;
        asprite[i].type = MISSILE_TYPE;
        asprite[i].state = HIDDEN;
        asprite[i].attack_step = 0;
        asprite[i].row = 0;
        asprite[i].col = 0;
        init_asprite_config(i);
    }

    // plane=1, affine sprite mode
    //xreg_vga_mode(4, 1, ASPRITE_CONFIGS, NUM_ASPRITES, 1);
    xregn(1, 0, 1, 5, 4, 1, ASPRITE_CONFIGS, NUM_ASPRITES, 1);
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
void reset_aliens()
{
    uint8_t i;

    for (i = ALIEN_BLUE_INDEX; i < ALIEN_MISSILE_INDEX; i++) {
        int16_t x, y;

        asprite[i].state = CRUISING;
        asprite[i].attack_step = 0;

        alien_white_x = ALIEN_WHITE_X;
        alien_red_x = ALIEN_RED_X;
        alien_pink_x = ALIEN_PINK_X;
        alien_yellow_x = ALIEN_YELLOW_X;
        alien_green_x = ALIEN_GREEN_X;
        alien_blue_x = ALIEN_BLUE_X;

        if (i == ALIEN_WHITE_INDEX + 1) {
            x = alien_white_x + 3*SHIP_SPACING_X;
            y = alien_white_y;
        } else if (i == ALIEN_WHITE_INDEX) {
            x = alien_white_x;
            y = alien_white_y;
        } else if (i >= ALIEN_RED_INDEX) {
            uint8_t n = i - ALIEN_RED_INDEX;
            x = alien_red_x + n*SHIP_SPACING_X;
            y = alien_red_y;
        } else if (i >= ALIEN_PINK_INDEX) {
            uint8_t n = i - ALIEN_PINK_INDEX;
            x = alien_pink_x + n*SHIP_SPACING_X;
            y = alien_pink_y;
        } else if (i >= ALIEN_YELLOW_INDEX) {
            uint8_t n = i - ALIEN_YELLOW_INDEX;
            x = alien_yellow_x + n*SHIP_SPACING_X;
            y = alien_yellow_y;
        }else if (i >= ALIEN_GREEN_INDEX) {
            uint8_t n = i - ALIEN_GREEN_INDEX;
            x = alien_green_x + n*SHIP_SPACING_X;
            y = alien_green_y;
        } else if (i >= ALIEN_BLUE_INDEX) {
            uint8_t n = i - ALIEN_BLUE_INDEX;
            x = alien_blue_x + n*SHIP_SPACING_X;
            y = alien_blue_y;

        }
        move_asprite(i, x, y);
        rotate_asprite(i, SHIP_SIZE, 180);
    }

    for (i = ALIEN_MISSILE_INDEX; i < EXPLOSION_INDEX; i++) {
        asprite[i].state = HIDDEN;
        move_asprite(i, 2*CANVAS_W, 2*CANVAS_H);
    }
}

// ----------------------------------------------------------------------------
// Move non-attacking aliens around a bit to avoid missiles (and amuse)
// Also checks if all aliens are dead, and starts next swarm if so
// ----------------------------------------------------------------------------
void update_aliens()
{
    uint8_t i;
    bool visible_aliens = false;
    bool state_changed = false;
    static alien_move state = FWD_AT_RIGHT;
    static int8_t transition_counter = -20;
    static uint8_t left_limit = ALIEN_BLUE_X - 2*SHIP_SPACING_X;
    static uint8_t right_limit = ALIEN_BLUE_X + 2*SHIP_SPACING_X;

    // Shift all the aliens back and forth to avoid stationary starship.
    if (state == MOVING_LEFT) {
        if (alien_blue_x <= left_limit) {
            state = FWD_AT_LEFT;
            state_changed = true;
            current_shift = 0;
            transition_counter = 0;
        }
    } else if (state == FWD_AT_LEFT) {
        if (transition_counter++ > 10) {
            state = MOVING_RIGHT;
            state_changed = true;
            current_shift = 1;
            transition_counter = 0;
            right_limit = ALIEN_BLUE_X + 2*SHIP_SPACING_X - lrand()%(3*SHIP_SPACING_X/2);
        }
    } else if (state == MOVING_RIGHT) {
        if (alien_blue_x >= right_limit) {
            state = FWD_AT_RIGHT;
            state_changed = true;
            current_shift = 0;
            transition_counter = 0;
        }
    } else if (state == FWD_AT_RIGHT) {
        if (transition_counter++ > 10) {
            state = MOVING_LEFT;
            state_changed = true;
            current_shift = -1;
            transition_counter = 0;
            left_limit = ALIEN_BLUE_X - 2*SHIP_SPACING_X + lrand()%(3*SHIP_SPACING_X/2);
        }
    }
    alien_blue_x += current_shift;
    alien_green_x += current_shift;
    alien_yellow_x += current_shift;
    alien_pink_x += current_shift;
    alien_red_x += current_shift;
    alien_white_x += current_shift;

    // Also randomly move each ship a bit in x, y,
    // and adjust each ship's heading, if appropriate.
    for (i = ALIEN_BLUE_INDEX; i < ALIEN_MISSILE_INDEX; i++) {
        if (asprite[i].state != HIDDEN) {
            visible_aliens = true;
            if (asprite[i].state == CRUISING || asprite[i].state == LAUNCHING) {
                int16_t x, y, dx, dy;
                dx = lrand()%5;
                dx -= 2; // either -2, -1, 0, 1, 2
                dy = lrand()%3;
                dy -= 1; // either -1, 0, 1

                if (asprite[i].state == CRUISING) {
                    if (i == ALIEN_WHITE_INDEX + 1) {
                        x = alien_white_x + 3*SHIP_SPACING_X + dx;
                        y = alien_white_y + dy;
                    } else if (i == ALIEN_WHITE_INDEX) {
                        x = alien_white_x + dx;
                        y = alien_white_y + dy;
                    } else if (i >= ALIEN_RED_INDEX) {
                        uint8_t n = i - ALIEN_RED_INDEX;
                        x = alien_red_x + n*SHIP_SPACING_X + dx;
                        y = alien_red_y + dy;
                    } else if (i >= ALIEN_PINK_INDEX) {
                        uint8_t n = i - ALIEN_PINK_INDEX;
                        x = alien_pink_x + n*SHIP_SPACING_X + dx;
                        y = alien_pink_y + dy;
                    } else if (i >= ALIEN_YELLOW_INDEX) {
                        uint8_t n = i - ALIEN_YELLOW_INDEX;
                        x = alien_yellow_x + n*SHIP_SPACING_X + dx;
                        y = alien_yellow_y + dy;
                    }else if (i >= ALIEN_GREEN_INDEX) {
                        uint8_t n = i - ALIEN_GREEN_INDEX;
                        x = alien_green_x + n*SHIP_SPACING_X + dx;
                        y = alien_green_y + dy;
                    } else if (i >= ALIEN_BLUE_INDEX) {
                        uint8_t n = i - ALIEN_BLUE_INDEX;
                        x = alien_blue_x + n*SHIP_SPACING_X + dx;
                        y = alien_blue_y + dy;
                    }
                } else { // asprite[i].state == LAUNCHING
                    // track swarm movement while launching
                    x = asprite[i].x_pos + current_shift;
                    y = asprite[i].y_pos;
                }
                move_asprite(i, x, y);

                if (state_changed && asprite[i].state == CRUISING) { // state just changed
                    rotate_asprite(i, SHIP_SIZE, 180 + current_shift*15);
                }
            }
        }
    }
    if (state_changed) {
        state_changed = false;
    }
    // this is a convenient place to check if all aliens are dead
    if (!visible_aliens) {
        current_swarm += 1;
        update_swarm();

        // reset the alien state
        state = FWD_AT_RIGHT;
        current_shift = 0;
        transition_counter = -20;
        left_limit = ALIEN_BLUE_X - 2*SHIP_SPACING_X;
        right_limit = ALIEN_BLUE_X + 2*SHIP_SPACING_X;
        reset_aliens();
    }
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
void fire_alien_missile(uint8_t i)
{
    uint8_t j;
    for (j=ALIEN_MISSILE_INDEX; j<EXPLOSION_INDEX; j++) {
        if (asprite[j].state == HIDDEN) {
            asprite[j].state = MOVING;
            asprite[j].x_pos = asprite[i].x_pos + HALF_SHIP_SIZE - 2;
            asprite[j].y_pos = asprite[i].y_pos + SHIP_SIZE - 1;
            move_asprite(j, asprite[j].x_pos, asprite[j].y_pos);
            return;
        }
    }
    printf("No alien missiles left!\n");
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
void proceed_with_evil_alien_plan()
{
    static uint8_t i; // alien index
    static int16_t dx[TOTAL_ASPRITES] = {0};

    for (i = ALIEN_BLUE_INDEX; i < ALIEN_MISSILE_INDEX; i++) {
        //printf("evil plan step = %u\n", step);
        switch (asprite[i].attack_step) {
            case 0: { // waiting
                break;
            }
            case 1: { // launch alien attack
                asprite[i].state = LAUNCHING;
                if(asprite[i].theta != 0) { // turn around
                    move_asprite(i, asprite[i].x_pos-1, asprite[i].y_pos+1);
                    rotate_asprite(i, SHIP_SIZE, asprite[i].theta-15);
                } else if (asprite[i].y_pos < (CANVAS_H>>2)) { // move straight down ...
                    move_asprite(i, asprite[i].x_pos, asprite[i].y_pos+1);
                } else { // ... until have reached CANVAS/4
                    asprite[i].attack_step = 2;
                }
                break;
            }
            case 2: { // adjust initial alien trajectory towards spaceship ...
                asprite[i].state = MOVING;
                if (asprite[i].y_pos < (CANVAS_H>>1)) { // ... until CANVAS_H/2 ...
                    dx[i] = (asprite[0].x_pos == asprite[i].x_pos) ? 0 :
                            ((asprite[0].x_pos < asprite[i].x_pos) ? -1 : 1);
                    rotate_asprite(i, SHIP_SIZE, 0 - dx[i]*45);
                    move_asprite(i, asprite[i].x_pos + dx[i], asprite[i].y_pos+1);
                } else {
                    if (dx[i] != 0) { // ... then fire missile, if not heading straight down
                        fire_alien_missile(i);
                    }
                    asprite[i].attack_step = 3;
                }
                break;
            }
            case 3: { // try to crash into spaceship on current heading
                int16_t tx, ty;
                move_asprite(i, asprite[i].x_pos + dx[i], asprite[i].y_pos+1);

                // check if collision occurred
                tx = asprite[i].x_pos - asprite[SPACESHIP_INDEX].x_pos;
                ty = asprite[i].y_pos - asprite[SPACESHIP_INDEX].y_pos;
                if (tx*tx+ty*ty < SHIP_SIZE*SHIP_SIZE) { // crash!

                    // move dead alien off the screen
                    asprite[i].y_pos += CANVAS_H;
                    asprite[i].state = HIDDEN;
                    asprite[i].attack_step = 0;
                    move_asprite(i, asprite[i].x_pos, asprite[i].y_pos);

                    // show explosion
                    asprite[EXPLOSION_INDEX].state = CRUISING;
                    asprite[EXPLOSION_INDEX].x_pos = asprite[SPACESHIP_INDEX].x_pos - HALF_SHIP_SIZE;
                    asprite[EXPLOSION_INDEX].y_pos = asprite[SPACESHIP_INDEX].y_pos - SHIP_SIZE;
                    move_asprite(EXPLOSION_INDEX, asprite[EXPLOSION_INDEX].x_pos, asprite[EXPLOSION_INDEX].y_pos);
                    current_shields -= 20;
                    update_shields();
                    explosion_timer = 0;

                } else if (asprite[i].y_pos >= CANVAS_H) { // is alien off the screen?
                    dx[i] = 0;
                    asprite[i].attack_step = 4;
                }
                break;
            }
            case 4: { // park
                rotate_asprite(i, SHIP_SIZE, 180 + current_shift*15);
                asprite[i].state = CRUISING;
                asprite[i].attack_step = 0;
                break;
            }
            default: { // go back to case 0
                asprite[i].attack_step = 0;
                break;
            }
        }
    }
}

// ----------------------------------------------------------------------------
// Find CRUISING alien closest to specified index, and launch attack
// ----------------------------------------------------------------------------
void launch_alien_attack(uint8_t i)
{
    bool any_cruising = false;

    if (asprite[i].state == CRUISING) {
        asprite[i].attack_step = 1;
    } else {
        int16_t dx, dy, d, dx_min=100, dy_min=100, d_min=10000;
        uint8_t j;
        for (j=ALIEN_BLUE_INDEX; j<ALIEN_MISSILE_INDEX; j++) {
            if (asprite[j].state == CRUISING) {
                any_cruising = true;
                dx = asprite[j].col - asprite[i].col;
                dy = asprite[j].row - asprite[i].row;
                d = dx*dx + dy*dy;
                if (d < d_min) {
                    d_min = d;
                    dx_min = dx;
                    dy_min = dy;
                }
            }
        }
        if (any_cruising) {
            for (j=ALIEN_BLUE_INDEX; j<ALIEN_MISSILE_INDEX; j++) {
                if (asprite[j].col == asprite[i].col+dx_min &&
                    asprite[j].row == asprite[i].row+dy_min   ) {
                    asprite[j].attack_step = 1;
                }
            }
        }
    }
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
uint8_t check_if_hit_alien(int16_t x, int16_t y)
{
    uint8_t i;

    for (i = ALIEN_BLUE_INDEX; i < ALIEN_MISSILE_INDEX; i++) {
        int16_t dx = (x + HALF_MISSILE_W) - (asprite[i].x_pos + HALF_SHIP_SIZE);
        int16_t dy = (y + HALF_MISSILE_H) - (asprite[i].y_pos + HALF_SHIP_SIZE);
        if (dx > -hit_threshold && dx < hit_threshold &&
            dy > -hit_threshold && dy < hit_threshold   ) {
            return i;
        }
    }
    return 0;
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
void update_starship_missile(int16_t x, int16_t y)
{
    uint8_t alien_hit = 0;

    if (asprite[EXPLOSION_INDEX].state == HIDDEN) {
        if (x >= (HALF_SHIP_SIZE-2) && x < (CANVAS_W-HALF_SHIP_SIZE-3)) {
            if (asprite[MISSILE_INDEX].state == MOVING) {
                if ((alien_hit = check_if_hit_alien(x, y)) != 0) {
                    // hit, so increment score!
                    current_score += 1;
                    update_score();

                    // move dead alien off the screen
                    asprite[alien_hit].y_pos += CANVAS_H;
                    asprite[alien_hit].state = HIDDEN;
                    asprite[alien_hit].attack_step = 0;
                    move_asprite(alien_hit, asprite[alien_hit].x_pos,
                                            asprite[alien_hit].y_pos);

                    // use this to trigger an attack,
                    launch_alien_attack(alien_hit);
                }
                if (alien_hit != 0 || y<0) {

                    // wasted missile, so decrement score!
                    if (!alien_hit) {
                        current_score -= 1;
                        update_score();
                    }

                    // need a new missile on ship
                    asprite[MISSILE_INDEX].state = CRUISING;
                    x = (asprite[SPACESHIP_INDEX].x_pos + HALF_SHIP_SIZE) - 2;
                    y =  CANVAS_H - SHIP_SIZE - MISSILE_H - 1;
                }
            }
            move_asprite(MISSILE_INDEX, x, y);
        }
    }
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
void update_missiles()
{
    uint8_t i;

    // update starship missile
    if (asprite[MISSILE_INDEX].state == MOVING) {
        update_starship_missile(asprite[MISSILE_INDEX].x_pos, asprite[MISSILE_INDEX].y_pos-2);
    }

    // update alien missiles
    for (i = ALIEN_MISSILE_INDEX; i < EXPLOSION_INDEX; i++ ) {
        if (asprite[i].state == MOVING) {
            // check for missile impact
            int16_t x = asprite[i].x_pos;
            int16_t y = asprite[i].y_pos + 1; // new y
            int16_t dx = (x + HALF_MISSILE_W) - (asprite[SPACESHIP_INDEX].x_pos + HALF_SHIP_SIZE);
            int16_t dy = (y + HALF_MISSILE_H) - (asprite[SPACESHIP_INDEX].y_pos + HALF_SHIP_SIZE);

            if (dx > -hit_threshold && dx < hit_threshold &&
                dy > -hit_threshold && dy < hit_threshold   ) { // a hit!

                // hide missile
                asprite[i].state = HIDDEN;
                x = CANVAS_W + 10;
                y = CANVAS_H + 10;

                // show explosion
                asprite[EXPLOSION_INDEX].state = CRUISING;
                asprite[EXPLOSION_INDEX].x_pos = asprite[SPACESHIP_INDEX].x_pos - HALF_SHIP_SIZE;
                asprite[EXPLOSION_INDEX].y_pos = asprite[SPACESHIP_INDEX].y_pos - SHIP_SIZE;
                move_asprite(EXPLOSION_INDEX, asprite[EXPLOSION_INDEX].x_pos, asprite[EXPLOSION_INDEX].y_pos);
                current_shields -= 20;
                update_shields();
                explosion_timer = 0;
            } else { // no hit
                if (y >= CANVAS_H) {
                    // hide missile
                    asprite[i].state = HIDDEN;
                    x = CANVAS_W + 10;
                    y = CANVAS_H + 10;
                }
            }
            move_asprite(i, x, y);
        }
    }
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
void update_ship(int16_t x, int16_t y)
{
    if (asprite[EXPLOSION_INDEX].state == HIDDEN) {
        if (x >= 0 && x < (CANVAS_W-SHIP_SIZE-1)) {
            move_asprite(SPACESHIP_INDEX, x, y);
        }
    }
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
void restart_game()
{
    // reset state variables to starting values
    movement_timer_threshold = MOVEMENT_TIMER_THRESHOLD_MAX;
    attack_timer_threshold = ATTACK_TIMER_THRESHOLD_MAX;
    current_swarm = 1;
    current_score = 0;
    current_shields = 100;
    update_swarm();
    update_score();
    update_shields();

    // move all alien asprites back into view
    reset_aliens();

    // clear explosion
    asprite[EXPLOSION_INDEX].state = HIDDEN;
    asprite[EXPLOSION_INDEX].x_pos = 2*CANVAS_W;
    asprite[EXPLOSION_INDEX].y_pos = 2*CANVAS_H;
    move_asprite(EXPLOSION_INDEX, asprite[EXPLOSION_INDEX].x_pos, asprite[EXPLOSION_INDEX].y_pos);

    // reset spaceship
    asprite[SPACESHIP_INDEX].state = CRUISING;
    move_asprite(SPACESHIP_INDEX, (CANVAS_W - SHIP_SIZE)/2 + 1, CANVAS_H - SHIP_SIZE - 1);

    // and its missile
    asprite[MISSILE_INDEX].state = CRUISING;
    move_asprite(MISSILE_INDEX, (CANVAS_W - MISSILE_W)/2, CANVAS_H - SHIP_SIZE - MISSILE_H - 1);

    // restart the game
    paused = false;
    game_over = false;
    update_paused();
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
int main()
{
    uint8_t v; // vsync counter, incements every 1/60 second, rolls over every 256
    uint16_t movement_timer = 0;
    uint16_t attack_timer = 0;
    bool handled_key = false;
    int16_t theta_deg = 0;

    // plane=1, canvas=1, w=320, h=240, bpp4
    init_bitmap_graphics(0xFF00, 0x0000, 1, 1, CANVAS_W, CANVAS_H, 4);

    erase_canvas();

    printf("Hello, from Star Swarms!\n");

    draw_text();
    init_the_universe();
    init_asprites();
    restart_game();

    // initialize keyboard
    //xreg_ria_keyboard(KEYBORD_INPUT);
    xregn( 0, 0, 0, 1, KEYBORD_INPUT);
    RIA.addr0 = KEYBORD_INPUT;
    RIA.step0 = 0;

    // vsync loop
    v = RIA.vsync;
    while (1) {
        uint8_t i;

        if (v == RIA.vsync) {
            continue; // wait until vsync is incremented
        } else {
            v = RIA.vsync; // new value for v
        }
        if (!paused) { // use these instead of v, because we can reset them
            movement_timer++;
            attack_timer++;
            explosion_timer++;
        }

        // Clear explosion when explosion_timer exceeds explosion_timer_threshold,
        // unless shields are down to 0%, in which case you are dead and game is over.
        if (!paused && (explosion_timer > explosion_timer_threshold)) {
            if (asprite[EXPLOSION_INDEX].state != HIDDEN) {
                if (current_shields > 0) {
                    // clear explosion
                    asprite[EXPLOSION_INDEX].state = HIDDEN;
                    asprite[EXPLOSION_INDEX].x_pos = 2*CANVAS_W;
                    asprite[EXPLOSION_INDEX].y_pos = 2*CANVAS_H;
                    move_asprite(EXPLOSION_INDEX, asprite[EXPLOSION_INDEX].x_pos, asprite[EXPLOSION_INDEX].y_pos);
                } else { // oops
                    game_over = true;
                    paused = true;
                    update_paused();
                }
            }
        }

        // Do stuff when movement_timer exceeds movement_timer_threshold
        if (!paused && (movement_timer > movement_timer_threshold)) {
            scroll_stars();
            update_aliens();
            movement_timer = 0; // reset it
        }

        if (!paused) {
            update_missiles();
        }

        // attack every time attack_timer exceeds attack_timer_threshold
        if (!paused && (attack_timer > attack_timer_threshold)) {
            launch_alien_attack(ALIEN_BLUE_INDEX+lrand()%NUM_ALIENS);
            attack_timer = 0; // reset it
        }

        if (!paused) {
            proceed_with_evil_alien_plan();
        }

        // fill the keystates bitmask array
        for (i = 0; i < KEYBOARD_BYTES; i++) {
            uint8_t new_keys;
            RIA.addr0 = KEYBORD_INPUT + i;
            new_keys = RIA.rw0;
            keystates[i] = new_keys;
        }

        // check for a key down
        if (!(keystates[0] & 1)) {
            if (!paused && key(KEY_RIGHT)) { // try to move ship right
                update_ship(asprite[SPACESHIP_INDEX].x_pos+1, asprite[SPACESHIP_INDEX].y_pos);
                if(asprite[MISSILE_INDEX].state != MOVING) {
                    update_starship_missile(asprite[MISSILE_INDEX].x_pos+1, asprite[MISSILE_INDEX].y_pos);
                }
            }
            if (!paused && key(KEY_LEFT)) { // try to move ship left
                update_ship(asprite[SPACESHIP_INDEX].x_pos-1, asprite[SPACESHIP_INDEX].y_pos);
                if(asprite[MISSILE_INDEX].state != MOVING) {
                    update_starship_missile(asprite[MISSILE_INDEX].x_pos-1, asprite[MISSILE_INDEX].y_pos);
                }
            }
            if (!paused &&
                (asprite[MISSILE_INDEX].state != MOVING) &&
                 (key(KEY_UP) || key(KEY_DOWN) || key(KEY_SPACE))) { // shoot
                asprite[MISSILE_INDEX].state = MOVING;
                update_starship_missile(asprite[MISSILE_INDEX].x_pos, asprite[MISSILE_INDEX].y_pos-2);
            }
            if (!handled_key && key(KEY_P)) { // pause
                paused = !paused;
                update_paused();
            } else if (!handled_key && key(KEY_R)) { // restart game
                restart_game();
            } else if (!handled_key && key(KEY_ESC)) { // exit game
                break;
            }
            handled_key = true;
        } else { // no keys down
            handled_key = false;
        }
    }
    //exit
    printf("Goodbye!\n");
}
