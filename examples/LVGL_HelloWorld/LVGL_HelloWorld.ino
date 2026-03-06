/**
 * LVGL_HelloWorld — Minimal LVGL example
 *
 * Demonstrates:
 *  - LVGL button with click counter
 *  - LVGL slider with value label
 *  - Touch input handled by LVGL
 *
 * Requires: LVGL library installed via Arduino Library Manager.
 * Copy lv_conf.h from the library root to your Arduino/libraries/ folder.
 */

#include <lvgl.h>
#include "WS_ESP32_Touch28.h"

// ── Slider with value label ─────────────────────────────────────────
static lv_obj_t * sliderLabel;

static void slider_event_cb(lv_event_t * e) {
  lv_obj_t * slider = lv_event_get_target_obj(e);
  lv_label_set_text_fmt(sliderLabel, "%" LV_PRId32, lv_slider_get_value(slider));
  lv_obj_align_to(sliderLabel, slider, LV_ALIGN_OUT_TOP_MID, 0, -15);
}

void create_slider(void) {
  lv_obj_t * slider = lv_slider_create(lv_screen_active());
  lv_obj_set_width(slider, 200);
  lv_obj_center(slider);
  lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

  sliderLabel = lv_label_create(lv_screen_active());
  lv_label_set_text(sliderLabel, "0");
  lv_obj_align_to(sliderLabel, slider, LV_ALIGN_OUT_TOP_MID, 0, -15);
}

// ── Button with click counter ───────────────────────────────────────
static void btn_event_cb(lv_event_t * e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t * btn = lv_event_get_target_obj(e);
  if (code == LV_EVENT_CLICKED) {
    static uint8_t cnt = 0;
    cnt++;
    lv_obj_t * label = lv_obj_get_child(btn, 0);
    lv_label_set_text_fmt(label, "Clicked: %d", cnt);
  }
}

void create_button(void) {
  lv_obj_t * btn = lv_button_create(lv_screen_active());
  lv_obj_set_pos(btn, 10, 10);
  lv_obj_set_size(btn, 120, 50);
  lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_ALL, NULL);

  lv_obj_t * label = lv_label_create(btn);
  lv_label_set_text(label, "Button");
  lv_obj_center(label);
}

// ── Setup & Loop ────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  WS_ESP32_Init();       // Initializes all hardware + LVGL (if installed)

  create_button();
  create_slider();

  Serial.println("LVGL_HelloWorld ready!");
}

void loop() {
  WS_ESP32_Loop();       // Processes touch, LVGL timers, LCD update
  delay(10);
}
