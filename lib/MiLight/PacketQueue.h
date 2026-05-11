#pragma once

#include <array>
#include <stddef.h>

#include <MiLightRadioConfig.h>
#include <MiLightRemoteConfig.h>

#ifndef MILIGHT_MAX_QUEUED_PACKETS
#define MILIGHT_MAX_QUEUED_PACKETS 40
#endif

struct QueuedPacket {
  uint8_t packet[MILIGHT_MAX_PACKET_LENGTH];
  const MiLightRemoteConfig* remoteConfig;
  size_t repeatsOverride;
};

// Fixed-capacity SPSC ring buffer. All storage is owned by the queue itself,
// so push/pop perform zero heap allocations — important on ESP8266 where
// per-packet make_shared churn fragments the ~40KB heap over time.
class PacketQueue {
public:
  PacketQueue();

  void push(const uint8_t* packet, const MiLightRemoteConfig* remoteConfig, const size_t repeatsOverride);

  // Copies the oldest queued packet into `out` and frees the slot. Returns
  // false if the queue was empty.
  bool pop(QueuedPacket& out);

  bool isEmpty() const;
  size_t size() const;
  size_t getDroppedPacketCount() const;

private:
  std::array<QueuedPacket, MILIGHT_MAX_QUEUED_PACKETS> slots;
  size_t head;
  size_t tail;
  size_t count;
  size_t droppedPackets;
};
