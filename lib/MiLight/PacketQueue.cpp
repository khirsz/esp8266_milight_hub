#include <PacketQueue.h>

#include <string.h>

PacketQueue::PacketQueue()
  : head(0)
  , tail(0)
  , count(0)
  , droppedPackets(0)
{ }

void PacketQueue::push(const uint8_t* packet, const MiLightRemoteConfig* remoteConfig, const size_t repeatsOverride) {
  if (count == MILIGHT_MAX_QUEUED_PACKETS) {
    // Queue full: drop the oldest entry so the freshest user intent
    // survives, mirroring the previous LinkedList-backed behavior.
    ++droppedPackets;
    tail = (tail + 1) % MILIGHT_MAX_QUEUED_PACKETS;
    --count;
  }

  QueuedPacket& slot = slots[head];
  memcpy(slot.packet, packet, remoteConfig->packetFormatter->getPacketLength());
  slot.remoteConfig = remoteConfig;
  slot.repeatsOverride = repeatsOverride;

  head = (head + 1) % MILIGHT_MAX_QUEUED_PACKETS;
  ++count;
}

bool PacketQueue::pop(QueuedPacket& out) {
  if (count == 0) {
    return false;
  }

  out = slots[tail];
  tail = (tail + 1) % MILIGHT_MAX_QUEUED_PACKETS;
  --count;
  return true;
}

bool PacketQueue::isEmpty() const {
  return count == 0;
}

size_t PacketQueue::size() const {
  return count;
}

size_t PacketQueue::getDroppedPacketCount() const {
  return droppedPackets;
}
