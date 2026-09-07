#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include <Wire.h>
#include <CST816S.h>
#include <Preferences.h>

// --- KOLORY (Tło ciemny grafit, akcent zielony/turkus) ---
#define COLOR_BG          0x121619
#define COLOR_CARD_BG     0x1D232A
#define COLOR_ACCENT      0x00FFC0
#define COLOR_TEXT_WHITE  0xFFFFFF
#define COLOR_TEXT_MUTED  0x9EA9B4

#define TFT_BL 2

// Funkcja opakowująca dla lv_tick_set_cb (LVGL 9)
static uint32_t my_tick_get_cb(void) {
    return (uint32_t)millis();
}

// --- OBIEKTY DISPLAY & TOUCH ---
Arduino_DataBus *bus = new Arduino_ESP32SPI(8, 9, 10, 11, -1);
Arduino_GFX *gfx = new Arduino_GC9A01(bus, 14, 0, true);
CST816S touch(6, 7, 13, 5);
Preferences prefs;

// --- ELEMENTY UI ---
lv_obj_t *tv;
lv_obj_t *tile_main, *tile_autostart, *tile_bms;

// Przełączniki główne (Stan aktualny)
lv_obj_t *sw_fridge, *sw_usbc, *sw_usba, *sw_inverter;

// Przełączniki Auto Start (Stan po uruchomieniu)
lv_obj_t *sw_auto_fridge, *sw_auto_usbc, *sw_auto_usba, *sw_auto_inverter;

lv_obj_t *slider_delay, *lbl_delay_val;

// --- ZMIENNE STANU ---
struct DeviceState {
  // Bieżący stan wyjść
  bool fridge = false;
  bool usbc = false;
  bool usba = false;
  bool inverter = false;

  // Konfiguracja Autostartu
  bool auto_fridge = false;
  bool auto_usbc = true;
  bool auto_usba = true;
  bool auto_inverter = false;
  int autoStartDelay = 5; // w sekundach
} devState;

// --- STYLE ---
static lv_style_t style_card;

// --- CALLBACKI UI ---
static void save_btn_event_cb(lv_event_t * e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    // Odczyt stanów z UI
    devState.fridge = lv_obj_has_state(sw_fridge, LV_STATE_CHECKED);
    devState.usbc = lv_obj_has_state(sw_usbc, LV_STATE_CHECKED);
    devState.usba = lv_obj_has_state(sw_usba, LV_STATE_CHECKED);
    devState.inverter = lv_obj_has_state(sw_inverter, LV_STATE_CHECKED);

    devState.auto_fridge = lv_obj_has_state(sw_auto_fridge, LV_STATE_CHECKED);
    devState.auto_usbc = lv_obj_has_state(sw_auto_usbc, LV_STATE_CHECKED);
    devState.auto_usba = lv_obj_has_state(sw_auto_usba, LV_STATE_CHECKED);
    devState.auto_inverter = lv_obj_has_state(sw_auto_inverter, LV_STATE_CHECKED);

    // Zapis do pamięci NVS
    prefs.putBool("fridge", devState.fridge);
    prefs.putBool("usbc", devState.usbc);
    prefs.putBool("usba", devState.usba);
    prefs.putBool("inverter", devState.inverter);

    prefs.putBool("a_fridge", devState.auto_fridge);
    prefs.putBool("a_usbc", devState.auto_usbc);
    prefs.putBool("a_usba", devState.auto_usba);
    prefs.putBool("a_inverter", devState.auto_inverter);
    prefs.putInt("delay", devState.autoStartDelay);

    // Animacja mignięcia ramki przycisku
    lv_obj_t * btn = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_set_style_border_color(btn, lv_color_hex(0xFFFFFF), 0);
    lv_timer_create([](lv_timer_t * t){
      lv_obj_t * b = (lv_obj_t *)lv_timer_get_user_data(t);
      if(b) lv_obj_set_style_border_color(b, lv_color_hex(COLOR_ACCENT), 0);
      lv_timer_delete(t);
    }, 300, btn);
  }
}

static void goto_autostart_cb(lv_event_t * e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    lv_obj_set_tile(tv, tile_autostart, LV_ANIM_ON);
  }
}

static void goto_main_cb(lv_event_t * e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    lv_obj_set_tile(tv, tile_main, LV_ANIM_ON);
  }
}

static void goto_bms_cb(lv_event_t * e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    lv_obj_set_tile(tv, tile_bms, LV_ANIM_ON);
  }
}

static void delay_slider_cb(lv_event_t * e) {
  lv_obj_t * slider = (lv_obj_t *)lv_event_get_target(e);
  devState.autoStartDelay = (int)lv_slider_get_value(slider);
  if (lbl_delay_val) {
    lv_label_set_text_fmt(lbl_delay_val, "ZWLOCA: %d s", devState.autoStartDelay);
  }
}

// --- GENEROWANIE PRZEŁĄCZNIKÓW ---
static lv_obj_t* create_switch_card(lv_obj_t * parent, const char * title, const char * icon_symbol, int16_t x, int16_t y, bool initial_state) {
  lv_obj_t * card = lv_obj_create(parent);
  lv_obj_remove_style_all(card);
  lv_obj_add_style(card, &style_card, 0);
  lv_obj_set_size(card, 96, 42);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_style_shadow_width(card, 10, 0);
  lv_obj_set_style_shadow_color(card, lv_color_hex(0x00FFC0), 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_20, 0);
  lv_obj_set_style_shadow_ofs_y(card, 3, 0);

  lv_obj_t * reflection = lv_obj_create(card);
  lv_obj_set_size(reflection, lv_pct(100), 2);
  lv_obj_align(reflection, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(reflection, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(reflection, LV_OPA_10, 0);
  lv_obj_set_style_radius(reflection, 10, 0);

  lv_obj_t * lbl_title = lv_label_create(card);
  lv_label_set_text(lbl_title, title);
  lv_obj_set_style_text_color(lbl_title, lv_color_hex(COLOR_TEXT_WHITE), 0);
  lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 10, 2);

  lv_obj_t * lbl_icon = lv_label_create(card);
  lv_label_set_text(lbl_icon, icon_symbol);
  lv_obj_set_style_text_color(lbl_icon, lv_color_hex(COLOR_ACCENT), 0);
  lv_obj_set_style_text_font(lbl_icon, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_icon, LV_ALIGN_LEFT_MID, 6, 4);

  lv_obj_t * sw = lv_switch_create(card);
  lv_obj_set_size(sw, 32, 16);
  lv_obj_align(sw, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
  
  lv_obj_set_style_bg_color(sw, lv_color_hex(COLOR_ACCENT), LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(sw, lv_color_hex(0x3A444E), LV_PART_MAIN);

  if (initial_state) {
    lv_obj_add_state(sw, LV_STATE_CHECKED);
  }
  return sw;
}

static lv_obj_t* create_bms_cell_card(lv_obj_t * parent, const char * cell_name, float voltage, int16_t x, int16_t y) {
  lv_obj_t * card = lv_obj_create(parent);
  lv_obj_remove_style_all(card);
  lv_obj_add_style(card, &style_card, 0);
  lv_obj_set_size(card, 96, 42);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_style_shadow_width(card, 8, 0);
  lv_obj_set_style_shadow_color(card, lv_color_hex(0x00FFC0), 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_20, 0);

  lv_obj_t * reflection = lv_obj_create(card);
  lv_obj_set_size(reflection, lv_pct(100), 2);
  lv_obj_align(reflection, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(reflection, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(reflection, LV_OPA_10, 0);
  lv_obj_set_style_radius(reflection, 10, 0);

  lv_obj_t * lbl_name = lv_label_create(card);
  lv_label_set_text_fmt(lbl_name, "%s", cell_name);
  lv_obj_set_style_text_color(lbl_name, lv_color_hex(COLOR_TEXT_WHITE), 0);
  lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_name, LV_ALIGN_TOP_MID, 0, 6);

  lv_obj_t * lbl_value = lv_label_create(card);
  lv_label_set_text_fmt(lbl_value, "%.2f V", voltage);
  lv_obj_set_style_text_color(lbl_value, lv_color_hex(COLOR_ACCENT), 0);
  lv_obj_set_style_text_font(lbl_value, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_value, LV_ALIGN_BOTTOM_MID, 0, -7);

  return card;
}

// --- SETUP UI ---
void setup_ui() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(COLOR_BG), 0);

  // Inicjalizacja stylu kart
  lv_style_init(&style_card);
  lv_style_set_bg_color(&style_card, lv_color_hex(COLOR_CARD_BG));
  lv_style_set_bg_opa(&style_card, LV_OPA_COVER);
  lv_style_set_radius(&style_card, 10);
  lv_style_set_border_width(&style_card, 1);
  lv_style_set_border_color(&style_card, lv_color_hex(0x2C353F));
  lv_style_set_shadow_width(&style_card, 12);
  lv_style_set_shadow_color(&style_card, lv_color_hex(0x00FFC0));
  lv_style_set_shadow_opa(&style_card, LV_OPA_20);
  lv_style_set_shadow_ofs_y(&style_card, 4);

  // Tileview do przełączania stron
  tv = lv_tileview_create(scr);
  lv_obj_set_style_bg_opa(tv, 0, 0);

  // -------------------------------------------------------------
  // EKRAN 1: GŁÓWNY KONTROLER ZASILANIA (Sterowanie Ręczne)
  // -------------------------------------------------------------
  tile_main = lv_tileview_add_tile(tv, 0, 0, LV_DIR_HOR);

  // Tytuł obniżony o 8px (z Y:10 na Y:18)
  lv_obj_t * lbl_header = lv_label_create(tile_main);
  lv_label_set_text(lbl_header, "PRZEKAZNIKI");
  lv_obj_set_style_text_font(lbl_header, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_header, lv_color_hex(COLOR_ACCENT), 0);
  lv_obj_align(lbl_header, LV_ALIGN_TOP_MID, 0, 18);

  // Przełączniki obniżone w pionie o 8px
  sw_fridge   = create_switch_card(tile_main, "LODOWKA", LV_SYMBOL_POWER, 20, 38, devState.fridge);
  sw_usbc     = create_switch_card(tile_main, "USB-C", LV_SYMBOL_USB, 124, 38, devState.usbc);
  sw_usba     = create_switch_card(tile_main, "USB-A", LV_SYMBOL_CHARGE, 20, 84, devState.usba);
  sw_inverter = create_switch_card(tile_main, "INWERTER", LV_SYMBOL_SETTINGS, 124, 84, devState.inverter);

  // Przycisk AUTO START przesunięty w dół na Y: 134
  lv_obj_t * btn_autostart = lv_btn_create(tile_main);
  lv_obj_set_size(btn_autostart, 170, 32);
  lv_obj_align(btn_autostart, LV_ALIGN_TOP_MID, 0, 142);
  lv_obj_set_style_bg_color(btn_autostart, lv_color_hex(COLOR_CARD_BG), 0);
  lv_obj_set_style_border_color(btn_autostart, lv_color_hex(COLOR_ACCENT), 0);
  lv_obj_set_style_border_width(btn_autostart, 1, 0);
  lv_obj_set_style_radius(btn_autostart, 16, 0);
  lv_obj_add_event_cb(btn_autostart, goto_autostart_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t * lbl_auto = lv_label_create(btn_autostart);
  lv_label_set_text(lbl_auto, "AUTO START");
  lv_obj_set_style_text_font(lbl_auto, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_auto, lv_color_hex(COLOR_TEXT_WHITE), 0);
  lv_obj_align(lbl_auto, LV_ALIGN_LEFT_MID, 10, 0);

  lv_obj_t * lbl_arrow = lv_label_create(btn_autostart);
  lv_label_set_text(lbl_arrow, LV_SYMBOL_RIGHT);
  lv_obj_set_style_text_color(lbl_arrow, lv_color_hex(COLOR_ACCENT), 0);
  lv_obj_align(lbl_arrow, LV_ALIGN_RIGHT_MID, -8, 0);

  lv_obj_t * btn_bms = lv_btn_create(tile_main);
  lv_obj_set_size(btn_bms, 170, 28);
  lv_obj_align(btn_bms, LV_ALIGN_TOP_MID, 0, 182);
  lv_obj_set_style_bg_color(btn_bms, lv_color_hex(COLOR_CARD_BG), 0);
  lv_obj_set_style_border_color(btn_bms, lv_color_hex(COLOR_ACCENT), 0);
  lv_obj_set_style_border_width(btn_bms, 1, 0);
  lv_obj_set_style_radius(btn_bms, 14, 0);
  lv_obj_add_event_cb(btn_bms, goto_bms_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t * lbl_bms = lv_label_create(btn_bms);
  lv_label_set_text(lbl_bms, "BMS NAPIĘCIA");
  lv_obj_set_style_text_font(lbl_bms, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_bms, lv_color_hex(COLOR_TEXT_WHITE), 0);
  lv_obj_center(lbl_bms);

  lv_obj_t * btn_save = lv_btn_create(tile_main);
  lv_obj_set_size(btn_save, 130, 30);
  lv_obj_align(btn_save, LV_ALIGN_TOP_MID, 0, 214);
  lv_obj_set_style_bg_color(btn_save, lv_color_hex(COLOR_CARD_BG), 0);
  lv_obj_set_style_border_color(btn_save, lv_color_hex(COLOR_ACCENT), 0);
  lv_obj_set_style_border_width(btn_save, 2, 0);
  lv_obj_set_style_radius(btn_save, 15, 0);
  lv_obj_add_event_cb(btn_save, save_btn_event_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t * lbl_save_icon = lv_label_create(btn_save);
  lv_label_set_text(lbl_save_icon, LV_SYMBOL_OK);
  lv_obj_set_style_text_color(lbl_save_icon, lv_color_hex(COLOR_ACCENT), 0);
  lv_obj_align(lbl_save_icon, LV_ALIGN_LEFT_MID, 12, 0);

  lv_obj_t * lbl_save_text = lv_label_create(btn_save);
  lv_label_set_text(lbl_save_text, "ZAPISZ");
  lv_obj_set_style_text_font(lbl_save_text, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_save_text, lv_color_hex(COLOR_TEXT_WHITE), 0);
  lv_obj_align(lbl_save_text, LV_ALIGN_LEFT_MID, 34, 0);

  // -------------------------------------------------------------
  // EKRAN 2: USTAWIENIA "AUTO START"
  // -------------------------------------------------------------
  tile_autostart = lv_tileview_add_tile(tv, 1, 0, LV_DIR_HOR);

  lv_obj_t * lbl_header_autostart = lv_label_create(tile_autostart);
  lv_label_set_text(lbl_header_autostart, "AUTO START");
  lv_obj_set_style_text_font(lbl_header_autostart, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_header_autostart, lv_color_hex(COLOR_ACCENT), 0);
  lv_obj_align(lbl_header_autostart, LV_ALIGN_TOP_MID, 0, 18);

  sw_auto_fridge   = create_switch_card(tile_autostart, "LODOWKA", LV_SYMBOL_POWER, 20, 38, devState.auto_fridge);
  sw_auto_usbc     = create_switch_card(tile_autostart, "USB-C", LV_SYMBOL_USB, 124, 38, devState.auto_usbc);
  sw_auto_usba     = create_switch_card(tile_autostart, "USB-A", LV_SYMBOL_CHARGE, 20, 84, devState.auto_usba);
  sw_auto_inverter = create_switch_card(tile_autostart, "INWERTER", LV_SYMBOL_SETTINGS, 124, 84, devState.auto_inverter);

  lbl_delay_val = lv_label_create(tile_autostart);
  lv_obj_set_style_text_font(lbl_delay_val, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_delay_val, lv_color_hex(COLOR_ACCENT), 0);
  lv_label_set_text_fmt(lbl_delay_val, "ZWLOCA: %d s", devState.autoStartDelay);
  lv_obj_align(lbl_delay_val, LV_ALIGN_TOP_MID, 0, 120);

  slider_delay = lv_slider_create(tile_autostart);
  lv_obj_set_size(slider_delay, 150, 10);
  lv_slider_set_range(slider_delay, 1, 30);
  lv_slider_set_value(slider_delay, devState.autoStartDelay, LV_ANIM_OFF);
  lv_obj_align(slider_delay, LV_ALIGN_TOP_MID, 0, 144);
  lv_obj_set_style_bg_color(slider_delay, lv_color_hex(COLOR_ACCENT), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider_delay, lv_color_hex(COLOR_ACCENT), LV_PART_KNOB);
  lv_obj_add_event_cb(slider_delay, delay_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

  lv_obj_t * btn_back = lv_btn_create(tile_autostart);
  lv_obj_set_size(btn_back, 100, 28);
  lv_obj_align(btn_back, LV_ALIGN_BOTTOM_MID, 0, -18);
  lv_obj_set_style_bg_color(btn_back, lv_color_hex(COLOR_CARD_BG), 0);
  lv_obj_set_style_border_color(btn_back, lv_color_hex(COLOR_ACCENT), 0);
  lv_obj_set_style_border_width(btn_back, 1, 0);
  lv_obj_set_style_radius(btn_back, 14, 0);
  lv_obj_add_event_cb(btn_back, goto_main_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t * lbl_back = lv_label_create(btn_back);
  lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " WRT");
  lv_obj_set_style_text_font(lbl_back, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_back, lv_color_hex(COLOR_TEXT_WHITE), 0);
  lv_obj_center(lbl_back);

  // -------------------------------------------------------------
  // EKRAN 3: BMS NAPIĘCIA
  // -------------------------------------------------------------
  tile_bms = lv_tileview_add_tile(tv, 2, 0, LV_DIR_HOR);

  lv_obj_t * lbl_bms_title = lv_label_create(tile_bms);
  lv_label_set_text(lbl_bms_title, "BMS NAPIĘCIA");
  lv_obj_set_style_text_font(lbl_bms_title, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_bms_title, lv_color_hex(COLOR_ACCENT), 0);
  lv_obj_align(lbl_bms_title, LV_ALIGN_TOP_MID, 0, 18);

  float cell_voltages[4] = {3.23f, 3.28f, 3.31f, 3.26f};
  float total_voltage = cell_voltages[0] + cell_voltages[1] + cell_voltages[2] + cell_voltages[3];
  float cell_delta = 0.08f;

  lv_obj_t * lbl_total = lv_label_create(tile_bms);
  lv_label_set_text_fmt(lbl_total, "%.2f V", total_voltage);
  lv_obj_set_style_text_font(lbl_total, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(lbl_total, lv_color_hex(COLOR_TEXT_WHITE), 0);
  lv_obj_align(lbl_total, LV_ALIGN_TOP_MID, 0, 38);

  lv_obj_t * lbl_delta = lv_label_create(tile_bms);
  lv_label_set_text_fmt(lbl_delta, "RÓŻNICA: %.2f V", cell_delta);
  lv_obj_set_style_text_font(lbl_delta, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_delta, lv_color_hex(COLOR_TEXT_MUTED), 0);
  lv_obj_align(lbl_delta, LV_ALIGN_TOP_MID, 0, 76);

  create_bms_cell_card(tile_bms, "1", cell_voltages[0], 20, 98);
  create_bms_cell_card(tile_bms, "2", cell_voltages[1], 124, 98);
  create_bms_cell_card(tile_bms, "3", cell_voltages[2], 20, 148);
  create_bms_cell_card(tile_bms, "4", cell_voltages[3], 124, 148);

  lv_obj_t * btn_bms_back = lv_btn_create(tile_bms);
  lv_obj_set_size(btn_bms_back, 100, 28);
  lv_obj_align(btn_bms_back, LV_ALIGN_BOTTOM_MID, 0, -18);
  lv_obj_set_style_bg_color(btn_bms_back, lv_color_hex(COLOR_CARD_BG), 0);
  lv_obj_set_style_border_color(btn_bms_back, lv_color_hex(COLOR_ACCENT), 0);
  lv_obj_set_style_border_width(btn_bms_back, 1, 0);
  lv_obj_set_style_radius(btn_bms_back, 14, 0);
  lv_obj_add_event_cb(btn_bms_back, goto_main_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t * lbl_bms_back = lv_label_create(btn_bms_back);
  lv_label_set_text(lbl_bms_back, LV_SYMBOL_LEFT " WRT");
  lv_obj_set_style_text_font(lbl_bms_back, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_bms_back, lv_color_hex(COLOR_TEXT_WHITE), 0);
  lv_obj_center(lbl_bms_back);
}
void setup() {
  Serial.begin(115200);

  // Wczytanie zapisanych stanów z pamięci NVS
  prefs.begin("power_ctrl", false);
  devState.fridge = prefs.getBool("fridge", false);
  devState.usbc = prefs.getBool("usbc", true);
  devState.usba = prefs.getBool("usba", true);
  devState.inverter = prefs.getBool("inverter", false);

  devState.auto_fridge = prefs.getBool("a_fridge", false);
  devState.auto_usbc = prefs.getBool("a_usbc", true);
  devState.auto_usba = prefs.getBool("a_usba", true);
  devState.auto_inverter = prefs.getBool("a_inverter", false);
  devState.autoStartDelay = prefs.getInt("delay", 5);

  pinMode(TFT_BL, OUTPUT); 
  digitalWrite(TFT_BL, HIGH);
  
  gfx->begin(); 
  gfx->fillScreen(BLACK);
  
  lv_init(); 
  lv_tick_set_cb(my_tick_get_cb);

  static uint8_t draw_buf[240 * 40 * 2];
  lv_display_t *disp = lv_display_create(240, 240);
  lv_display_set_flush_cb(disp, [](lv_display_t *d, const lv_area_t *a, uint8_t *px) {
    gfx->draw16bitBeRGBBitmap(a->x1, a->y1, (uint16_t *)px, lv_area_get_width(a), lv_area_get_height(a));
    lv_display_flush_ready(d);
  });
  lv_display_set_buffers(disp, draw_buf, nullptr, sizeof(draw_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);

  touch.begin();
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, [](lv_indev_t *i, lv_indev_data_t *d){
    if(touch.available()){ 
      d->state = LV_INDEV_STATE_PRESSED; 
      d->point.x = touch.data.x; 
      d->point.y = touch.data.y; 
    } else {
      d->state = LV_INDEV_STATE_RELEASED;
    }
  });

  // Tworzenie interfejsu
  setup_ui();
}

void loop() {
  lv_timer_handler();
  delay(5);
}