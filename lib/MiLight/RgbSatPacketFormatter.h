#include <RgbPacketFormatter.h>

#ifndef _RGB_SAT_PACKET_FORMATTER_H
#define _RGB_SAT_PACKET_FORMATTER_H

#define RGB_SAT_TOTAL_MODES 15
#define RGB_SAT_RESET_HUE   240  // blue, in degrees

struct RgbSatModeMapping {
  const char* name;
  uint8_t mode;
};

extern const RgbSatModeMapping RGB_SAT_MODE_MAP[RGB_SAT_TOTAL_MODES];

class RgbSatPacketFormatter : public RgbPacketFormatter {
public:
  RgbSatPacketFormatter() : RgbPacketFormatter(REMOTE_TYPE_RGB_SAT) {}

  virtual void updateMode(uint8_t mode);

  static int modeFromName(const char* name);

private:
  void resetToMode0();
};

#endif
