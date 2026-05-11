#include <PacketSender.h>
#include <MiLightRadioConfig.h>

PacketSender::PacketSender(
  RadioSwitchboard& radioSwitchboard,
  Settings& settings,
  PacketSentHandler packetSentHandler
) : radioSwitchboard(radioSwitchboard)
  , settings(settings)
  , hasCurrentPacket(false)
  , packetRepeatsRemaining(0)
  , packetSentHandler(packetSentHandler)
  , lastSend(0)
  , currentResendCount(settings.packetRepeats)
  , throttleMultiplier(
      std::ceil(
        (settings.packetRepeatThrottleSensitivity / 1000.0) * settings.packetRepeats
      )
    )
  , nextPacketAllowedAt(0)
{ }

void PacketSender::enqueue(uint8_t* packet, const MiLightRemoteConfig* remoteConfig, const size_t repeatsOverride) {
#ifdef DEBUG_PRINTF
  Serial.println("Enqueuing packet");
#endif
  size_t repeats = repeatsOverride == DEFAULT_PACKET_SENDS_VALUE
    ? this->currentResendCount
    : repeatsOverride;

  queue.push(packet, remoteConfig, repeats);
}

void PacketSender::loop() {
  // Switch to the next packet if we're done with the current one,
  // but only after the inter-packet quiet gap has elapsed so the
  // receiver can register each press as a discrete event.
  if (packetRepeatsRemaining == 0 && !queue.isEmpty() && (long)(millis() - nextPacketAllowedAt) >= 0) {
    nextPacket();
  }

  // If there's a packet we're handling, deal with it
  if (hasCurrentPacket && packetRepeatsRemaining > 0) {
    handleCurrentPacket();
  }
}

bool PacketSender::isSending() {
  return packetRepeatsRemaining > 0 || !queue.isEmpty();
}

void PacketSender::nextPacket() {
#ifdef DEBUG_PRINTF
  Serial.printf("Switching to next packet, %d packets in queue\n", queue.size());
#endif
  hasCurrentPacket = queue.pop(currentPacket);
  if (!hasCurrentPacket) {
    return;
  }

  if (currentPacket.repeatsOverride > 0) {
    packetRepeatsRemaining = currentPacket.repeatsOverride;
  } else {
    packetRepeatsRemaining = settings.packetRepeats;
  }

  // Adjust resend count according to throttling rules
  updateResendCount();
}

void PacketSender::handleCurrentPacket() {
  // Always switch radio.  could've been listening in another context
  radioSwitchboard.switchRadio(currentPacket.remoteConfig);

  size_t numToSend = std::min(packetRepeatsRemaining, settings.packetRepeatsPerLoop);
  sendRepeats(numToSend);
  packetRepeatsRemaining -= numToSend;

  // If we're done sending this packet, arm the inter-packet gap so the
  // next packet (if any) waits before transmitting, then fire the callback.
  if (packetRepeatsRemaining == 0) {
    nextPacketAllowedAt = millis() + MILIGHT_INTER_PACKET_GAP_MS;
    if (packetSentHandler != nullptr) {
      packetSentHandler(currentPacket.packet, *currentPacket.remoteConfig);
    }

    hasCurrentPacket = false;
  }
}

size_t PacketSender::queueLength() const {
  return queue.size();
}

size_t PacketSender::droppedPackets() const {
  return queue.getDroppedPacketCount();
}

void PacketSender::sendRepeats(size_t num) {
  size_t len = currentPacket.remoteConfig->packetFormatter->getPacketLength();

#ifdef DEBUG_PRINTF
  Serial.printf_P(PSTR("Sending packet (%d repeats): \n"), num);
  for (size_t i = 0; i < len; i++) {
    Serial.printf_P(PSTR("%02X "), currentPacket.packet[i]);
  }
  Serial.println();
  int iStart = millis();
#endif

  for (size_t i = 0; i < num; ++i) {
    radioSwitchboard.write(currentPacket.packet, len);
  }

#ifdef DEBUG_PRINTF
  int iElapsed = millis() - iStart;
  Serial.print("Elapsed: ");
  Serial.println(iElapsed);
#endif
}

void PacketSender::updateResendCount() {
  unsigned long now = millis();
  long millisSinceLastSend = now - lastSend;
  long x = (millisSinceLastSend - settings.packetRepeatThrottleThreshold);
  long delta = x * throttleMultiplier;
  int signedResends = static_cast<int>(this->currentResendCount) + delta;

  if (signedResends < static_cast<int>(settings.packetRepeatMinimum)) {
    signedResends = settings.packetRepeatMinimum;
  } else if (signedResends > static_cast<int>(settings.packetRepeats)) {
    signedResends = settings.packetRepeats;
  }

  this->currentResendCount = signedResends;
  this->lastSend = now;
}