/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/services/notifications/notification_style.h>

#include <string.h>

#include <pbl/services/notifications/alerts_preferences_private.h>

#include <applib/preferred_content_size.h>
#include <shell/system_theme.h>

static const int16_t s_ladder[] = {14, 18, 24, 28};

uint8_t notification_style_text_size(void) {
  PreferredContentSize size = alerts_preferences_get_notification_content_size();
  if (size == NotificationContentSizeSystem) {
    size = system_theme_get_content_size();
  }
  const int step = (int)size - (int)PreferredContentSizeDefault;
  return (step < 0) ? 0 : ((step > 0) ? 2 : 1);
}

int16_t notification_style_scaled_points(int16_t points) {
  int index = 0;
  while ((index < 3) && (s_ladder[index] < points)) {
    index++;
  }
  index += (int)notification_style_text_size() - 1;
  index = (index < 0) ? 0 : ((index > 3) ? 3 : index);
  return s_ladder[index];
}

GFont notification_style_gothic(int16_t points, bool bold) {
  const char *key;
  switch (points) {
    case 14:
      key = bold ? FONT_KEY_GOTHIC_14_BOLD : FONT_KEY_GOTHIC_14;
      break;
    case 18:
      key = bold ? FONT_KEY_GOTHIC_18_BOLD : FONT_KEY_GOTHIC_18;
      break;
    case 24:
      key = bold ? FONT_KEY_GOTHIC_24_BOLD : FONT_KEY_GOTHIC_24;
      break;
    default:
      key = bold ? FONT_KEY_GOTHIC_28_BOLD : FONT_KEY_GOTHIC_28;
      break;
  }
  return fonts_get_system_font(key);
}

GFont notification_style_scaled_font(int16_t points, bool bold) {
  return notification_style_gothic(notification_style_scaled_points(points), bold);
}

GColor notification_style_bg(void) {
  return system_theme_is_dark_mode() ? GColorBlack : GColorWhite;
}

GColor notification_style_fg(void) {
  return system_theme_is_dark_mode() ? GColorWhite : GColorBlack;
}

GColor notification_style_secondary(void) {
#if PBL_COLOR
  return system_theme_is_dark_mode() ? GColorLightGray : GColorDarkGray;
#else
  return notification_style_fg();
#endif
}

const char *notification_style_body_text(const char *header, const char *body) {
  if (!body) {
    return "";
  }
  const size_t length = header ? strlen(header) : 0;
  if ((length > 0) && (strncmp(body, header, length) == 0)) {
    if ((body[length] == ':') && (body[length + 1] == ' ')) {
      return body + length + 2;
    }
    if (body[length] == '\n') {
      return body + length + 1;
    }
  }
  return body;
}
