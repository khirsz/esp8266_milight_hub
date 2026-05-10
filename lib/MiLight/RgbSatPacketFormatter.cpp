#include <RgbSatPacketFormatter.h>
#include <GroupStateStore.h>
#include <string.h>

const RgbSatModeMapping RGB_SAT_MODE_MAP[RGB_SAT_TOTAL_MODES] = {
  {"normal",              0},
  {"white",               1},
  {"low_saturation",      2},
  {"pulsing",             3},
  {"pulsing_rgbw",        4},
  {"blink_rgbw",          5},
  {"blink_yellow_white",  6},
  {"blink_yellow_blue",   7},
  {"blink_yellow_blue",   8},
  {"blink_yellow_green",  9},
  {"blink_red",           10},
  {"blink_blue",          11},
  {"blink_green",         12},
  {"blink_yellow",        13},
  {"demo",                14},
};

int RgbSatPacketFormatter::modeFromName(const char* name) {
  for (size_t i = 0; i < RGB_SAT_TOTAL_MODES; i++) {
    if (strcmp(name, RGB_SAT_MODE_MAP[i].name) == 0) {
      return static_cast<int>(RGB_SAT_MODE_MAP[i].mode);
    }
  }
  return -1;
}

void RgbSatPacketFormatter::resetToMode0() {
  // 3-packet reset that lands at mode 0 from any starting mode.
  // Mode 2 (lower sat) ignores hue resets, so we drop into mode 1 (white)
  // via previous_mode, then the second hue write resets that to mode 0.
  // For all other modes the first hue already reaches mode 0; the
  // subsequent previous_mode clamps at 0 and the second hue is a no-op.
  updateHue(RGB_SAT_RESET_HUE);
  previousMode();
  updateHue(RGB_SAT_RESET_HUE);
}

void RgbSatPacketFormatter::updateMode(uint8_t mode) {
  if (mode >= RGB_SAT_TOTAL_MODES) {
    return;
  }

  // Reflect the mode in GroupState.saturation so HA shows a sensible value.
  // The next onPacketSentHandler full-state publish will broadcast it.
  if (this->stateStore != NULL) {
    GroupState* state = this->stateStore->get(deviceId, groupId, this->deviceType);
    if (state != NULL) {
      uint8_t saturation;
      switch (mode) {
        case 1:  saturation = 0;   break;  // white
        case 2:  saturation = 50;  break;  // low_saturation
        default: saturation = 100; break;  // normal + animations
      }
      state->setSaturation(saturation);
    }
  }

  resetToMode0();
  for (uint8_t i = 0; i < mode; i++) {
    nextMode();
  }
}
