/*
    Copyright (C) 2016 Pebble Technology Corp.

    This file is part of Revolver.

    Revolver is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Revolver.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "enamel.h"
#include "watch_model.h"
#include <pebble-events/pebble-events.h>
#include <ctype.h>

static Window *window;
static Layer *clock_layer;

static GFont digital_font;
static ClockState s_clock_state;

void watch_model_handle_center_change(const CenterState *state) {
  s_clock_state.center_state = *state;
  layer_mark_dirty(clock_layer);
}

void watch_model_handle_dot_change(const DotState *state) {
  DotType old_dot_type = s_clock_state.dot_states[state->ordinal].dot_type;
  s_clock_state.dot_states[state->ordinal] = *state;
  s_clock_state.dot_states[state->ordinal].dot_type = old_dot_type;
  layer_mark_dirty(clock_layer);
}

void watch_model_handle_time_change(const struct tm *tick_time) {
  s_clock_state.current_time = *tick_time;
  for (int i = 0; i < NUM_DOTS_TOTAL; i++) {
    s_clock_state.dot_states[i].dot_type = get_dot_type(i, tick_time);
  }
  layer_mark_dirty(clock_layer);
}

void watch_model_handle_color_config_change(const ColorConfig *config) {
  s_clock_state.color_config = *config;
  layer_mark_dirty(clock_layer);
}

static void draw_dot(GContext *ctx, const GPoint *center, uint32_t distance, uint32_t radius, uint32_t angle,
                     GColor dot_color) {
  graphics_context_set_fill_color(ctx, dot_color);
  GRect dot_rect = GRect(center->x - distance, center->y - distance, distance * 2, distance * 2);
  GPoint dot_center = gpoint_from_polar(dot_rect, GOvalScaleModeFitCircle, angle);
  graphics_fill_circle(ctx, dot_center, radius);
}

static void draw_dots(GContext *ctx, const GPoint *center, const ClockState *current_state) {
  for (int32_t i = 0; i < NUM_DOTS_TOTAL; i++) {
    DotState dot_state = current_state->dot_states[i];
    bool drawn = false;
    if (dot_state.dot_type & DotType_HOUR_DOT) {
      draw_dot(ctx, center, dot_state.distance, HOUR_DOT_RADIUS, dot_state.angle,
               current_state->color_config.hour_dot_color);
      drawn = true;
    }
    if (dot_state.dot_type & DotType_MINUTE_DOT) {
      draw_dot(ctx, center, dot_state.distance, MINUTE_DOT_RADIUS, dot_state.angle,
               current_state->color_config.minute_dot_color);
      drawn = true;
    }
    if (!drawn) {
      draw_dot(ctx, center, dot_state.distance, NEUTRAL_DOT_RADIUS, dot_state.angle,
               current_state->color_config.neutral_dot_color);
    }
  }
}

static void draw_center(GContext *ctx, const GRect *layer_bounds, const ClockState *current_state) {
    graphics_context_set_fill_color(ctx, enamel_get_center_color());
    int32_t center_radius = CENTER_RADIUS;
    GRect circle_frame = (GRect) { .size = GSize(center_radius * 2, center_radius * 2) };
    grect_align(&circle_frame, layer_bounds, GAlignCenter, false /* clips */);
    graphics_fill_radial(ctx, circle_frame, GOvalScaleModeFitCircle, center_radius, 0,
                         TRIG_MAX_ANGLE);
}

static void draw_circle(GContext *ctx, const GRect *layer_bounds, int32_t radius) {
    graphics_context_set_fill_color(ctx, enamel_get_bg_color());
    int32_t center_radius = CENTER_RADIUS - radius + 3;
    GRect circle_frame = (GRect) { .size = GSize(CENTER_RADIUS * 2 + 6, CENTER_RADIUS * 2 + 6) };
    grect_align(&circle_frame, layer_bounds, GAlignCenter, false /* clips */);
    graphics_fill_radial(ctx, circle_frame, GOvalScaleModeFitCircle, center_radius, 0, TRIG_MAX_ANGLE);
}

static void draw_clock(Layer *layer, GContext *ctx) {
  const ClockState *current_state = &s_clock_state;
  GRect layer_bounds = layer_get_bounds(layer);
  GPoint center_point = grect_center_point(&layer_bounds);
  graphics_context_set_fill_color(ctx, enamel_get_bg_color());
  graphics_fill_rect(ctx, layer_bounds, 0, (GCornerMask)NULL);
  draw_dots(ctx, &center_point, current_state);
  draw_center(ctx, &layer_bounds, current_state);
  time_t now = time(NULL);
  struct tm *current_time = localtime(&now);
  int32_t offset = 0;

  // clock
  static char s_time_string[10];
  char *s_time_string_trimmed = &s_time_string[0];
  if (clock_is_24h_style()) {
      strftime(s_time_string, sizeof(s_time_string), "%H:%M", current_time);
  }
  else {
      strftime(s_time_string, sizeof(s_time_string), enamel_get_leading_zero() ? "%I:%M" : "%l:%M", current_time);
      if (!enamel_get_leading_zero()) {
          while(isspace((unsigned char)*s_time_string_trimmed))
              s_time_string_trimmed = s_time_string_trimmed + 1;
      }
  }
  digital_font = fonts_get_system_font(enamel_get_clock_font());
  GSize text_size = graphics_text_layout_get_content_size(s_time_string_trimmed,
                                                          digital_font, layer_bounds,
                                                          GTextOverflowModeFill,
                                                          GTextAlignmentCenter);
  graphics_context_set_text_color(ctx, enamel_get_text_color());

  // date
  if (enamel_get_display_date()) {
      static char s_date_string[10];
      offset = text_size.h;
      strftime(s_date_string, sizeof(s_date_string), "%a %d", localtime(&now));
      char *c = s_date_string;
      while (*c) {
          *c = toupper((unsigned char)*c);
	  c++;
      }
      GFont date_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
      GSize date_size = graphics_text_layout_get_content_size(s_date_string, date_font, layer_bounds,
                                                              GTextOverflowModeFill,
                                                              GTextAlignmentCenter);
      graphics_context_set_text_color(ctx, enamel_get_text_color());
      GRect date_text_box = GRect(center_point.x - date_size.w / 2,
		                  center_point.y - date_size.h * 2 / 3 + offset * .6,
                                  date_size.w, date_size.h);
      graphics_draw_text(ctx, s_date_string, date_font, date_text_box, GTextOverflowModeFill,
                         GTextAlignmentCenter, NULL);
  }
  GRect text_box = GRect(center_point.x - text_size.w / 2,
		         center_point.y - text_size.h * 2 / 3 - offset * .1,
		         text_size.w, text_size.h);
  graphics_draw_text(ctx, s_time_string_trimmed, digital_font, text_box, GTextOverflowModeFill,
                     GTextAlignmentCenter, NULL);

  int32_t center_radius = current_state->center_state.center_radius;
  if (center_radius < CENTER_RADIUS) {
      draw_circle(ctx, &layer_bounds, center_radius);
  }

}

static void prv_app_did_focus(bool did_focus) {
  if (!did_focus) {
    return;
  }

  app_focus_service_unsubscribe();

  watch_model_init();
  watch_model_start_intro();
}

static void window_load(Window *window) {
  Layer *const window_layer = window_get_root_layer(window);
  const GRect bounds = layer_get_bounds(window_layer);

  // TODO: Configurable?
  //window_set_background_color(window, enamel_get_bg_color());

  clock_layer = layer_create(bounds);
  layer_set_update_proc(clock_layer, draw_clock);
  layer_add_child(window_layer, clock_layer);
  //tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  digital_font = fonts_get_system_font(FONT_KEY_LECO_26_BOLD_NUMBERS_AM_PM);
}

static void window_unload(Window *window) {
  layer_destroy(clock_layer);
}

static void init(void) {
  enamel_init(0, 0);
  window = window_create();
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(window, true /* animated */);
  app_focus_service_subscribe_handlers((AppFocusHandlers) {
    .did_focus = prv_app_did_focus,
  });
  events_app_message_open();
}

static void deinit(void) {
  enamel_deinit();
  watch_model_deinit();
  window_destroy(window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
