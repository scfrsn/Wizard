#include <pebble.h>
#include "globals.h"
#include "localize.h"

static Window *window;

#define STRING_LENGTH 255
#define NUM_ICON_IMAGES	7

typedef enum {WEATHER_LAYER, CALENDAR_LAYER, MUSIC_LAYER, NUM_LAYERS} AnimatedLayers;

static PropertyAnimation *ani_out, *ani_in;

static TextLayer *text_weather_cond_layer, *text_weather_temp_layer;
static TextLayer *text_date_layer, *text_time_layer;
static TextLayer *text_mail_layer, *text_sms_layer, *text_phone_layer;
static TextLayer *calendar_date_layer, *calendar_text_layer;
static TextLayer *music_artist_layer, *music_song_layer;
static TextLayer *text_battery_layer, *text_pebble_battery_layer;

static Layer *battery_info_layer, *battery_layer, *pebble_battery_layer;
static Layer *mail_layer, *sms_layer, *phone_layer, *message_layer, *animated_layer[4];

static BitmapLayer *background_image, *icon_image;
GBitmap *bg_image;
GBitmap *icon_imgs[NUM_ICON_IMAGES];

static char date_text[] = "                                  ";
static char day_text[]  = "                                  ";
static char string_buffer[STRING_LENGTH];
static char pebble_buffer[STRING_LENGTH];
static char calendar_date_str[STRING_LENGTH], calendar_text_str[STRING_LENGTH];
static char music_artist_str[STRING_LENGTH], music_title_str[STRING_LENGTH];
static char weather_temp_str[6], sms_count_str[5], mail_count_str[5], phone_count_str[5];
static int icon_img, batteryPercent, batteryPblPercent, active_layer;

const int ICON_IMG_IDS[] = {
  RESOURCE_ID_IMAGE_ICON_SIRI,
  RESOURCE_ID_IMAGE_ICON_REFRESH,
  RESOURCE_ID_IMAGE_ICON_ACTIVATOR,
  RESOURCE_ID_IMAGE_ICON_DISCONNECTED,
  RESOURCE_ID_IMAGE_ICON_PLAY_PAUSE,
  RESOURCE_ID_IMAGE_ICON_NEXT,
  RESOURCE_ID_IMAGE_ICON_PREVIOUS
};

static uint32_t s_sequence_number = 0xFFFFFFFE;

AppMessageResult sm_message_out_get(DictionaryIterator **iter_out) {
  AppMessageResult result = app_message_outbox_begin(iter_out);
  if(result != APP_MSG_OK) return result;
  dict_write_int32(*iter_out, SM_SEQUENCE_NUMBER_KEY, ++s_sequence_number);
  if(s_sequence_number == 0xFFFFFFFF) {
    s_sequence_number = 1;
  }
  return APP_MSG_OK;
}

/* We can't include ctype.h in Pebble projects (that I'm aware of),
so we're using this to convert the case on our localized date strings. */

char* date_case(char* text) {

  // Convert it to lowercase only if the locale is French or Spanish.

  if (strcmp("fr_FR", i18n_get_system_locale()) == 0 || strcmp("es_ES", i18n_get_system_locale()) == 0) {
    int length = strlen(text);
    char* result = malloc(length); // malloc adds the 0 automatically at result[length]
    int i;
    for (i = 0; i < length; i++)
      if ((text[i] >= 65) && (text[i] <= 90))
        result[i] = text[i] | 32;
      else
        result[i] = text[i];
        return result;
  } else {
    return text;
  }
}

void reset() {
  if (bluetooth_connection_service_peek() == 1) {
    layer_set_hidden(animated_layer[WEATHER_LAYER], false);
    layer_set_hidden(animated_layer[MUSIC_LAYER], false);
    layer_set_hidden(animated_layer[CALENDAR_LAYER], false);
    layer_set_hidden(message_layer, true);
  }
  layer_set_hidden(battery_info_layer, true);
  layer_set_hidden(battery_layer, false);
  layer_set_hidden(pebble_battery_layer, false);
  text_layer_set_text(text_date_layer, date_case(date_text));
}

void reset_sequence_number() {
  DictionaryIterator *iter = NULL;
  app_message_outbox_begin(&iter);
  if(!iter) return;
  dict_write_int32(iter, SM_SEQUENCE_NUMBER_KEY, 0xFFFFFFFF);
  app_message_outbox_send();
}

void sendCommand(int key) {
	DictionaryIterator* iterout;
	sm_message_out_get(&iterout);
    if(!iterout) return;

	dict_write_int8(iterout, key, -1);
	app_message_outbox_send();
}

void sendCommandInt(int key, int param) {
	DictionaryIterator* iterout;
	sm_message_out_get(&iterout);
    if(!iterout) return;

	dict_write_int8(iterout, key, param);
	app_message_outbox_send();
}

void handle_minute_tick(struct tm *tick_time, TimeUnits units_changed) {

  setlocale(LC_ALL, i18n_get_system_locale());

  // Need to be static because they're used by the system later.

  static char time_text[] = "00:00";
  char *time_format;

  // Localized date strings.

  strftime(day_text, sizeof(day_text), "%A", tick_time);

  if (strcmp("fr_FR", i18n_get_system_locale()) == 0) {            // French
    strftime(date_text, sizeof(date_text), "%e %B", tick_time);
  } else if (strcmp("de_DE", i18n_get_system_locale()) == 0) {     // German
    strftime(date_text, sizeof(date_text), "%e. %B", tick_time);
  } else if (strcmp("es_ES", i18n_get_system_locale()) == 0) {     // Spanish
    strftime(date_text, sizeof(date_text), "%e de %B", tick_time);
  } else {                                                         // English
    strftime(date_text, sizeof(date_text), "%B %e", tick_time);
  }

  text_layer_set_text(text_date_layer, date_case(date_text));

  if (clock_is_24h_style()) {
    time_format = "%R";
  } else {
    time_format = "%I:%M";
  }

  strftime(time_text, sizeof(time_text), time_format, tick_time);

  // Kludge to handle lack of non-padded hour format string for twelve hour clock.

  if (!clock_is_24h_style() && (time_text[0] == '0')) {
    memmove(time_text, &time_text[1], sizeof(time_text) - 1);
  }

  text_layer_set_text(text_time_layer, time_text);

}

void notification(int image, int vibration) {
  if (bluetooth_connection_service_peek() == 1) {
    bitmap_layer_set_bitmap(icon_image, icon_imgs[image]);
    layer_set_hidden(animated_layer[WEATHER_LAYER], true);
    layer_set_hidden(animated_layer[MUSIC_LAYER], true);
    layer_set_hidden(animated_layer[CALENDAR_LAYER], true);
    layer_set_hidden(message_layer, false);
    if (vibration == 1) {
      static const uint32_t const segments[] = { 50 };
      VibePattern pat = {
        .durations = segments,
        .num_segments = ARRAY_LENGTH(segments),
      };
      vibes_enqueue_custom_pattern(pat);
    } else if (vibration == 2) {
      static const uint32_t const segments[] = { 50, 100, 50 };
      VibePattern pat = {
        .durations = segments,
        .num_segments = ARRAY_LENGTH(segments),
      };
      vibes_enqueue_custom_pattern(pat);
    }
    app_timer_register(1000,reset,NULL);
  }
}

void inbox_received_callback(DictionaryIterator *received, void *context) {

  Tuple *t = dict_read_first(received);
  char *weather_cond;

  while(t != NULL) {
    switch (t->key) {

      // Weather Temperature

      case SM_WEATHER_TEMP_KEY:
        memcpy(weather_temp_str, t->value->cstring, strlen(t->value->cstring));
        weather_temp_str[strlen(t->value->cstring)] = '\0';
        text_layer_set_text(text_weather_temp_layer, weather_temp_str);
      break;

      // Weather Condition
      
      case SM_WEATHER_ICON_KEY:

        /* Instead of displaying weather icons, we're going to use these
        codes as simplified weather conditions to make translation easier.
        Using a weather API with multilingual support would be ideal (for
        example, openweathermap.org), but the weather fetching is performed
        in the Smartwatch+ phone app so we'll work with what we have :) */

        if      (t->value->uint8 == 0) { weather_cond = _("Clear Skies"); }
        else if (t->value->uint8 == 1) { weather_cond = _("Raining"); }
        else if (t->value->uint8 == 2) { weather_cond = _("Cloudy"); }
        else if (t->value->uint8 == 3) { weather_cond = _("Partly Cloudy"); }
        else if (t->value->uint8 == 4) { weather_cond = _("Foggy"); }
        else if (t->value->uint8 == 5) { weather_cond = _("Windy"); }
        else if (t->value->uint8 == 6) { weather_cond = _("Snowing"); }
        else if (t->value->uint8 == 7) { weather_cond = _("Stormy"); }
        else                           { weather_cond = _("It's Currently"); }

        text_layer_set_text(text_weather_cond_layer, weather_cond);
      break;

      // Missed Phone Calls
      case SM_COUNT_PHONE_KEY:
        memcpy(phone_count_str, t->value->cstring, strlen(t->value->cstring));
        phone_count_str[strlen(t->value->cstring)] = '\0';
        if (phone_count_str[0] == '0') {
          layer_set_hidden(phone_layer, true);
        } else {
          text_layer_set_text(text_phone_layer, phone_count_str);
          layer_set_hidden(phone_layer, false);
        }
      break;

      // Unread Messages
      case SM_COUNT_SMS_KEY:
        memcpy(sms_count_str, t->value->cstring, strlen(t->value->cstring));
        sms_count_str[strlen(t->value->cstring)] = '\0';
        if (sms_count_str[0] == '0') {
          layer_set_hidden(sms_layer, true);
        } else {
          text_layer_set_text(text_sms_layer, sms_count_str);
          layer_set_hidden(sms_layer, false);
        }
      break;

      // Unread Emails
      case SM_COUNT_MAIL_KEY:
      memcpy(mail_count_str, t->value->cstring, strlen(t->value->cstring));
      mail_count_str[strlen(t->value->cstring)] = '\0';
      if (mail_count_str[0] == '0') {
        layer_set_hidden(mail_layer, true);
      } else {
        text_layer_set_text(text_mail_layer, mail_count_str);
        layer_set_hidden(mail_layer, false);
      }
      break;

      // Phone Battery Percentage
      case SM_COUNT_BATTERY_KEY:
      batteryPercent = t->value->uint8;
      layer_mark_dirty(battery_layer);
      snprintf(string_buffer, sizeof(string_buffer), "%d", batteryPercent);
      text_layer_set_text(text_battery_layer, string_buffer);
      break;

      // Next Calendar Event Time
      case SM_STATUS_CAL_TIME_KEY:
      memcpy(calendar_date_str, t->value->cstring, strlen(t->value->cstring));
      calendar_date_str[strlen(t->value->cstring)] = '\0';
      text_layer_set_text(calendar_date_layer, calendar_date_str);
      break;

      // Next Calendar Event Title
      case SM_STATUS_CAL_TEXT_KEY:
      memcpy(calendar_text_str, t->value->cstring, strlen(t->value->cstring));
      calendar_text_str[strlen(t->value->cstring)] = '\0';
      text_layer_set_text(calendar_text_layer, calendar_text_str);
      break;

      // Current Song Artist
      case SM_STATUS_MUS_ARTIST_KEY:
        memcpy(music_artist_str, t->value->cstring, strlen(t->value->cstring));
        music_artist_str[strlen(t->value->cstring)] = '\0';

        if (strcmp(music_artist_str, "No Artist") == 0) {
          text_layer_set_text(music_artist_layer, _("No Artist"));
        } else {
          text_layer_set_text(music_artist_layer, music_artist_str);
        }
      break;

      // Current Song Title
      case SM_STATUS_MUS_TITLE_KEY:
        memcpy(music_title_str, t->value->cstring, strlen(t->value->cstring));
        music_title_str[strlen(t->value->cstring)] = '\0';

        if (strcmp(music_title_str, "No Title") == 0) {
          text_layer_set_text(music_song_layer, _("No Title"));
        } else {
          text_layer_set_text(music_song_layer, music_title_str);
        }
      break;

    }

    // Get next pair, if any
    t = dict_read_next(received);
  }

}

// TAP / ACCELEROMETER HANDLER

void tap_handler(AccelAxisType axis, int32_t direction) {
  layer_set_hidden(battery_layer, true);
  layer_set_hidden(pebble_battery_layer, true);
  layer_set_hidden(battery_info_layer, false);
  text_layer_set_text(text_date_layer, date_case(day_text));
  app_timer_register(3500,reset,NULL);
}

// SELECT KEY HANDLERS

void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  ani_out = property_animation_create_layer_frame(animated_layer[active_layer], &GRect(0, 76, 144, 45), &GRect(-144, 76, 144, 45));
  animation_schedule((Animation*)ani_out);
  active_layer = (active_layer + 1) % (NUM_LAYERS);
  ani_in = property_animation_create_layer_frame(animated_layer[active_layer], &GRect(144, 76, 144, 45), &GRect(0, 76, 144, 45));
  animation_schedule((Animation*)ani_in);
}

void select_multi_click_handler(ClickRecognizerRef recognizer, void *context) {
  uint8_t clicks = click_number_of_clicks_counted(recognizer);
  if (clicks == 2) {
    sendCommandInt(SM_ACTIVATOR_KEY_PRESSED, ACTIVATOR_KEY_PRESSED_SELECT);
    notification(2,2);
  } else if (clicks == 3) {
    sendCommandInt(SM_ACTIVATOR_KEY_PRESSED, ACTIVATOR_KEY_HELD_SELECT);
    notification(2,2);
  }
}

void select_long_click_handler(ClickRecognizerRef recognizer, void *context) {
  sendCommand(SM_PLAYPAUSE_KEY);
  notification(4,0);
}

void select_long_click_release_handler(ClickRecognizerRef recognizer, void *context) {}

// UP KEY HANDLERS

void up_click_handler(ClickRecognizerRef recognizer, void *context) {
  sendCommand(SM_OPEN_SIRI_KEY);
  notification(0,0);
}

void up_multi_click_handler(ClickRecognizerRef recognizer, void *context) {
  uint8_t clicks = click_number_of_clicks_counted(recognizer);
  if (clicks == 2) {
    sendCommandInt(SM_ACTIVATOR_KEY_PRESSED, ACTIVATOR_KEY_PRESSED_UP);
    notification(2,1);
  } else if (clicks == 3) {
    sendCommandInt(SM_ACTIVATOR_KEY_PRESSED, ACTIVATOR_KEY_HELD_UP);
    notification(2,2);
  }
}

void up_long_click_handler(ClickRecognizerRef recognizer, void *context) {
  sendCommand(SM_PREVIOUS_TRACK_KEY);
  notification(6,0);
}

void up_long_click_release_handler(ClickRecognizerRef recognizer, void *context) {}

// DOWN KEY HANDLERS

void down_click_handler(ClickRecognizerRef recognizer, void *context) {
  sendCommandInt(SM_SCREEN_ENTER_KEY, STATUS_SCREEN_APP);
  notification(1,0);
}

void down_multi_click_handler(ClickRecognizerRef recognizer, void *context) {
  uint8_t clicks = click_number_of_clicks_counted(recognizer);
  if (clicks == 2) {
    sendCommandInt(SM_ACTIVATOR_KEY_PRESSED, ACTIVATOR_KEY_PRESSED_DOWN);
    notification(2,1);
  } else if (clicks == 3) {
    sendCommandInt(SM_ACTIVATOR_KEY_PRESSED, ACTIVATOR_KEY_HELD_DOWN);
    notification(2,2);
  }
}

void down_long_click_handler(ClickRecognizerRef recognizer, void *context) {
  sendCommand(SM_NEXT_TRACK_KEY);
  notification(5,0);
}

void down_long_click_release_handler(ClickRecognizerRef recognizer, void *context) {}

void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
  window_multi_click_subscribe(BUTTON_ID_SELECT, 2, 10, 0, true, select_multi_click_handler);
  window_multi_click_subscribe(BUTTON_ID_UP, 2, 10, 0, true, up_multi_click_handler);
  window_multi_click_subscribe(BUTTON_ID_DOWN, 2, 10, 0, true, down_multi_click_handler);
  window_long_click_subscribe(BUTTON_ID_SELECT, 500, select_long_click_handler, select_long_click_release_handler);
  window_long_click_subscribe(BUTTON_ID_UP, 500, up_long_click_handler, up_long_click_release_handler);
  window_long_click_subscribe(BUTTON_ID_DOWN, 500, down_long_click_handler, down_long_click_release_handler);
}

void battery_layer_update_callback(Layer *me, GContext* ctx) {
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect((int)((batteryPercent/100.0)*16.0)-16, 0, 16, 8), 0, GCornerNone);
}

void pebble_battery_layer_update_callback(Layer *me, GContext* ctx) {
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect((int)((batteryPblPercent/100.0)*16.0)-16, 0, 16, 8), 0, GCornerNone);
}

static void window_load(Window *window) {}

static void window_unload(Window *window) {}

static void window_appear(Window *window) {
	sendCommandInt(SM_SCREEN_ENTER_KEY, STATUS_SCREEN_APP);
}

static void window_disappear(Window *window) {
	sendCommandInt(SM_SCREEN_EXIT_KEY, STATUS_SCREEN_APP);
}

void reconnect(void *data) {
	reset();
	sendCommandInt(SM_SCREEN_ENTER_KEY, STATUS_SCREEN_APP);
}

void bluetoothChanged(bool connected) {
  if (connected) {
    app_timer_register(5000, reconnect, NULL);
    reset();
  } else {
    bitmap_layer_set_bitmap(icon_image, icon_imgs[3]);

    // Set the phone battery to 0%.

    batteryPercent = 0;
    layer_mark_dirty(battery_layer);

    layer_set_hidden(animated_layer[WEATHER_LAYER], true);
    layer_set_hidden(animated_layer[MUSIC_LAYER], true);
    layer_set_hidden(animated_layer[CALENDAR_LAYER], true);
    layer_set_hidden(message_layer, false);

    // Un-hide the following layers so we can cover up the checkmarks.

    layer_set_hidden(phone_layer, false);
    text_layer_set_text(text_phone_layer, "");
    layer_set_hidden(sms_layer, false);
    text_layer_set_text(text_sms_layer, "");
    layer_set_hidden(mail_layer, false);
    text_layer_set_text(text_mail_layer, "");
    text_layer_set_text(text_battery_layer, "");

    vibes_double_pulse();
  }
}

void batteryChanged(BatteryChargeState batt) {
  batteryPblPercent = batt.charge_percent;
  snprintf(pebble_buffer, sizeof(pebble_buffer), "%d", batteryPblPercent);
  text_layer_set_text(text_pebble_battery_layer, pebble_buffer);
  layer_mark_dirty(pebble_battery_layer);
}

static void init(void) {
  window = window_create();
  window_set_fullscreen(window, true);

  window_set_click_config_provider(window, click_config_provider);
  window_set_window_handlers(window, (WindowHandlers) {
       .load = window_load,
     .unload = window_unload,
	   .appear = window_appear,
	.disappear = window_disappear
  });
  const bool animated = true;
  window_stack_push(window, animated);

  // Init icon images

  for (int i=0; i<NUM_ICON_IMAGES; i++) {
    icon_imgs[i] = gbitmap_create_with_resource(ICON_IMG_IDS[i]);
  }

  bg_image = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BACKGROUND);
  Layer *window_layer = window_get_root_layer(window);
  GRect bg_bounds = layer_get_frame(window_layer);

  background_image = bitmap_layer_create(bg_bounds);
  layer_add_child(window_layer, bitmap_layer_get_layer(background_image));
  bitmap_layer_set_bitmap(background_image, bg_image);

  text_date_layer = text_layer_create(bg_bounds);
  text_layer_set_text_alignment(text_date_layer, GTextAlignmentCenter);
  text_layer_set_text_color(text_date_layer, GColorWhite);
  text_layer_set_background_color(text_date_layer, GColorClear);
  layer_set_frame(text_layer_get_layer(text_date_layer), GRect(0, 2, 144, 24));
  text_layer_set_font(text_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  layer_add_child(window_layer, text_layer_get_layer(text_date_layer));

  text_time_layer = text_layer_create(bg_bounds);
  text_layer_set_text_alignment(text_time_layer, GTextAlignmentCenter);
  text_layer_set_text_color(text_time_layer, GColorWhite);
  text_layer_set_background_color(text_time_layer, GColorClear);
  layer_set_frame(text_layer_get_layer(text_time_layer), GRect(0, 20, 144, 50));
  text_layer_set_font(text_time_layer, fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_SQUARE_48)));
  layer_add_child(window_layer, text_layer_get_layer(text_time_layer));

  animated_layer[WEATHER_LAYER] = layer_create(GRect(0, 76, 144, 45));
  layer_add_child(window_layer, animated_layer[WEATHER_LAYER]);

  text_weather_cond_layer = text_layer_create(GRect(6, -1, 132, 21));
  text_layer_set_text_alignment(text_weather_cond_layer, GTextAlignmentCenter);
  text_layer_set_text_color(text_weather_cond_layer, GColorWhite);
  text_layer_set_background_color(text_weather_cond_layer, GColorClear);
  text_layer_set_font(text_weather_cond_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  layer_add_child(animated_layer[WEATHER_LAYER], text_layer_get_layer(text_weather_cond_layer));
  text_layer_set_text(text_weather_cond_layer, _("Waiting for")); // "Waiting for"

  text_weather_temp_layer = text_layer_create(GRect(6, 15, 132, 28));
  text_layer_set_text_alignment(text_weather_temp_layer, GTextAlignmentCenter);
  text_layer_set_text_color(text_weather_temp_layer, GColorWhite);
  text_layer_set_background_color(text_weather_temp_layer, GColorClear);
  text_layer_set_font(text_weather_temp_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  layer_add_child(animated_layer[WEATHER_LAYER], text_layer_get_layer(text_weather_temp_layer));
  text_layer_set_text(text_weather_temp_layer, _("Weather")); // "Weather"

  animated_layer[CALENDAR_LAYER] = layer_create(GRect(144, 76, 144, 45));
  layer_add_child(window_layer, animated_layer[CALENDAR_LAYER]);

  calendar_date_layer = text_layer_create(GRect(6, -1, 132, 21));
  text_layer_set_text_alignment(calendar_date_layer, GTextAlignmentCenter);
  text_layer_set_text_color(calendar_date_layer, GColorWhite);
  text_layer_set_background_color(calendar_date_layer, GColorClear);
  text_layer_set_font(calendar_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  layer_add_child(animated_layer[CALENDAR_LAYER], text_layer_get_layer(calendar_date_layer));
  text_layer_set_text(calendar_date_layer, _("No Upcoming")); // "No Upcoming"

  calendar_text_layer = text_layer_create(GRect(6, 15, 132, 28));
  text_layer_set_text_alignment(calendar_text_layer, GTextAlignmentCenter);
  text_layer_set_text_color(calendar_text_layer, GColorWhite);
  text_layer_set_background_color(calendar_text_layer, GColorClear);
  text_layer_set_font(calendar_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  layer_add_child(animated_layer[CALENDAR_LAYER], text_layer_get_layer(calendar_text_layer));
  text_layer_set_text(calendar_text_layer, _("Appointments")); // "Appointment"

  animated_layer[MUSIC_LAYER] = layer_create(GRect(144, 76, 144, 45));
  layer_add_child(window_layer, animated_layer[MUSIC_LAYER]);

  music_artist_layer = text_layer_create(GRect(6, -1, 132, 21));
  text_layer_set_text_alignment(music_artist_layer, GTextAlignmentCenter);
  text_layer_set_text_color(music_artist_layer, GColorWhite);
  text_layer_set_background_color(music_artist_layer, GColorClear);
  text_layer_set_font(music_artist_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  layer_add_child(animated_layer[MUSIC_LAYER], text_layer_get_layer(music_artist_layer));
  text_layer_set_text(music_artist_layer, _("No Artist")); // "Artist"

  music_song_layer = text_layer_create(GRect(6, 15, 132, 28));
  text_layer_set_text_alignment(music_song_layer, GTextAlignmentCenter);
  text_layer_set_text_color(music_song_layer, GColorWhite);
  text_layer_set_background_color(music_song_layer, GColorClear);
  text_layer_set_font(music_song_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  layer_add_child(animated_layer[MUSIC_LAYER], text_layer_get_layer(music_song_layer));
  text_layer_set_text(music_song_layer, _("No Title")); // "Title"

  mail_layer = layer_create(GRect(63, 128, 30, 18));
  layer_add_child(window_layer, mail_layer);
  layer_set_clips(mail_layer,true);

  text_mail_layer = text_layer_create(GRect(0, -2, 30, 18));
  text_layer_set_text_alignment(text_mail_layer, GTextAlignmentCenter);
  text_layer_set_text_color(text_mail_layer, GColorBlack);
  text_layer_set_background_color(text_mail_layer, GColorWhite);
  text_layer_set_font(text_mail_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  layer_add_child(mail_layer, text_layer_get_layer(text_mail_layer));
  text_layer_set_text(text_mail_layer, "");

  sms_layer = layer_create(GRect(31, 128, 30, 18));
  layer_add_child(window_layer, sms_layer);
  layer_set_clips(sms_layer,true);

  text_sms_layer = text_layer_create(GRect(0, -2, 30, 18));
  text_layer_set_text_alignment(text_sms_layer, GTextAlignmentCenter);
  text_layer_set_text_color(text_sms_layer, GColorBlack);
  text_layer_set_background_color(text_sms_layer, GColorWhite);
  text_layer_set_font(text_sms_layer,  fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  layer_add_child(sms_layer, text_layer_get_layer(text_sms_layer));
  text_layer_set_text(text_sms_layer, "");

  phone_layer = layer_create(GRect(-1, 128, 30, 18));
  layer_add_child(window_layer, phone_layer);
  layer_set_clips(phone_layer,true);

  text_phone_layer = text_layer_create(GRect(0, -2, 30, 18));
  text_layer_set_text_alignment(text_phone_layer, GTextAlignmentCenter);
  text_layer_set_text_color(text_phone_layer, GColorBlack);
  text_layer_set_background_color(text_phone_layer, GColorWhite);
  text_layer_set_font(text_phone_layer,  fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  layer_add_child(phone_layer, text_layer_get_layer(text_phone_layer));
  text_layer_set_text(text_phone_layer, "");

  battery_info_layer = layer_create(GRect(0, 130, 144, 48));
  layer_add_child(window_layer, battery_info_layer);

  text_battery_layer = text_layer_create(GRect(97, 15, 28, 20));
  text_layer_set_text_alignment(text_battery_layer, GTextAlignmentRight);
  text_layer_set_text_color(text_battery_layer, GColorBlack);
  text_layer_set_background_color(text_battery_layer, GColorWhite);
  text_layer_set_font(text_battery_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  layer_add_child(battery_info_layer, text_layer_get_layer(text_battery_layer));
  text_layer_set_text(text_battery_layer, "");

  text_pebble_battery_layer = text_layer_create(GRect(97, -2, 28, 20));
  text_layer_set_text_alignment(text_pebble_battery_layer, GTextAlignmentRight);
  text_layer_set_text_color(text_pebble_battery_layer, GColorBlack);
  text_layer_set_background_color(text_pebble_battery_layer, GColorWhite);
  text_layer_set_font(text_pebble_battery_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  layer_add_child(battery_info_layer, text_layer_get_layer(text_pebble_battery_layer));
  text_layer_set_text(text_pebble_battery_layer, "");

  layer_set_hidden(battery_info_layer, true);

  battery_layer = layer_create(GRect(104, 153, 16, 8));
  layer_set_update_proc(battery_layer, battery_layer_update_callback);
  layer_add_child(window_layer, battery_layer);

  batteryPercent = 0;
  layer_mark_dirty(battery_layer);

  pebble_battery_layer = layer_create(GRect(104, 136, 16, 8));
  layer_set_update_proc(pebble_battery_layer, pebble_battery_layer_update_callback);
  layer_add_child(window_layer, pebble_battery_layer);

  BatteryChargeState pbl_batt = battery_state_service_peek();
  batteryPblPercent = pbl_batt.charge_percent;
  snprintf(pebble_buffer, sizeof(pebble_buffer), "%d", batteryPblPercent);
  text_layer_set_text(text_pebble_battery_layer, pebble_buffer);
  layer_mark_dirty(pebble_battery_layer);

  message_layer = layer_create(GRect(0, 76, 144, 45));
  layer_add_child(window_layer, message_layer);

  icon_image = bitmap_layer_create(GRect(52, 2, 40, 40));
  layer_add_child(message_layer, bitmap_layer_get_layer(icon_image));
  bitmap_layer_set_bitmap(icon_image, icon_imgs[icon_img]);

  layer_set_hidden(message_layer, true);

  active_layer = WEATHER_LAYER;

  tick_timer_service_subscribe(MINUTE_UNIT, handle_minute_tick);
	bluetooth_connection_service_subscribe(bluetoothChanged);
	battery_state_service_subscribe(batteryChanged);
  accel_tap_service_subscribe(tap_handler);
}

static void deinit(void) {
  animation_destroy((Animation*)ani_in);
  animation_destroy((Animation*)ani_out);
  text_layer_destroy(text_weather_cond_layer);
  text_layer_destroy(text_weather_temp_layer);
  text_layer_destroy(text_date_layer);
  text_layer_destroy(text_time_layer);
  text_layer_destroy(text_mail_layer);
  text_layer_destroy(text_sms_layer);
  text_layer_destroy(text_phone_layer);
  text_layer_destroy(calendar_date_layer);
  text_layer_destroy(calendar_text_layer);
  text_layer_destroy(music_artist_layer);
  text_layer_destroy(music_song_layer);
  text_layer_destroy(text_battery_layer);
  text_layer_destroy(text_pebble_battery_layer);
  layer_destroy(battery_info_layer);
  layer_destroy(battery_layer);
  layer_destroy(pebble_battery_layer);
  layer_destroy(mail_layer);
  layer_destroy(sms_layer);
  layer_destroy(phone_layer);
  layer_destroy(message_layer);

	for (int i=0; i<NUM_LAYERS; i++) {
		layer_destroy(animated_layer[i]);
	}

  for (int i=0; i<NUM_ICON_IMAGES; i++) {
    gbitmap_destroy(icon_imgs[i]);
  }

	gbitmap_destroy(bg_image);

	tick_timer_service_unsubscribe();
	bluetooth_connection_service_unsubscribe();
	battery_state_service_unsubscribe();
  accel_tap_service_unsubscribe();

  window_destroy(window);
}

int main(void) {
	app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum() );
	app_message_register_inbox_received(inbox_received_callback);

  init();
  locale_init();

  app_event_loop();
	app_message_deregister_callbacks();

  deinit();
}
