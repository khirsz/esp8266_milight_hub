#include <stddef.h>
#include <string.h>
#include <algorithm>
#include <MqttClient.h>
#include <TokenIterator.h>
#include <UrlTokenBindings.h>
#include <IntParsing.h>
#include <ArduinoJson.h>
#include <WiFiClient.h>
#include <MiLightRadioConfig.h>
#include <AboutHelper.h>


static const char* STATUS_CONNECTED = "connected";
static const char* STATUS_DISCONNECTED = "disconnected_clean";
static const char* STATUS_LWT_DISCONNECTED = "disconnected_unclean";

namespace {
  // Returns the total length of the placeholder token (including the
  // leading ':') if `p` points at ":<name>" where the character after
  // <name> is a non-identifier byte (i.e. a real token boundary).
  // Returns 0 otherwise. This prevents `:device_id` from matching inside
  // `:device_id_extended`.
  inline size_t matchToken(const char* p, const char* name, size_t nameLen) {
    if (strncmp(p + 1, name, nameLen) != 0) {
      return 0;
    }
    const char next = p[1 + nameLen];
    const bool isIdent =
      next == '_'
      || (next >= '0' && next <= '9')
      || (next >= 'A' && next <= 'Z')
      || (next >= 'a' && next <= 'z');
    return isIdent ? 0 : nameLen + 1;
  }

  inline void appendBounded(char*& w, char* limit, const char* src, size_t n) {
    const size_t avail = limit > w ? static_cast<size_t>(limit - w) : 0;
    const size_t copy = std::min(n, avail);
    memcpy(w, src, copy);
    w += copy;
  }
}

MqttClient::MqttClient(Settings& settings, MiLightClient*& milightClient)
  : mqttClient(tcpClient),
    milightClient(milightClient),
    settings(settings),
    lastConnectAttempt(0),
    connected(false)
{
  String strDomain = settings.mqttServer();
  this->domain = new char[strDomain.length() + 1];
  strcpy(this->domain, strDomain.c_str());
}

MqttClient::~MqttClient() {
  String aboutStr = generateConnectionStatusMessage(STATUS_DISCONNECTED);
  mqttClient.publish(settings.mqttClientStatusTopic.c_str(), aboutStr.c_str(), true);
  mqttClient.disconnect();
  delete[] this->domain;
}

void MqttClient::onConnect(OnConnectFn fn) {
  this->onConnectFn = fn;
}

void MqttClient::begin() {
#ifdef MQTT_DEBUG
  printf_P(
    PSTR("MqttClient - Connecting to: %s\nparsed:%s:%u\n"),
    settings._mqttServer.c_str(),
    settings.mqttServer().c_str(),
    settings.mqttPort()
  );
#endif

  mqttClient.setServer(this->domain, settings.mqttPort());
  mqttClient.setCallback(
    [this](char* topic, byte* payload, int length) {
      this->publishCallback(topic, payload, length);
    }
  );
  reconnect();
}

bool MqttClient::connect() {
  char nameBuffer[30];
  sprintf_P(nameBuffer, PSTR("milight-hub-%u"), getESPId());

#ifdef MQTT_DEBUG
    Serial.println(F("MqttClient - connecting using name"));
    Serial.println(nameBuffer);
#endif

  if (settings.mqttUsername.length() > 0 && settings.mqttClientStatusTopic.length() > 0) {
    return mqttClient.connect(
      nameBuffer,
      settings.mqttUsername.c_str(),
      settings.mqttPassword.c_str(),
      settings.mqttClientStatusTopic.c_str(),
      2,
      true,
      generateConnectionStatusMessage(STATUS_LWT_DISCONNECTED).c_str()
    );
  } else if (settings.mqttUsername.length() > 0) {
    return mqttClient.connect(
      nameBuffer,
      settings.mqttUsername.c_str(),
      settings.mqttPassword.c_str()
    );
  } else if (settings.mqttClientStatusTopic.length() > 0) {
    return mqttClient.connect(
      nameBuffer,
      settings.mqttClientStatusTopic.c_str(),
      2,
      true,
      generateConnectionStatusMessage(STATUS_LWT_DISCONNECTED).c_str()
    );
  } else {
    return mqttClient.connect(nameBuffer);
  }
}

void MqttClient::sendBirthMessage() {
  if (settings.mqttClientStatusTopic.length() > 0) {
    String aboutStr = generateConnectionStatusMessage(STATUS_CONNECTED);
    mqttClient.publish(settings.mqttClientStatusTopic.c_str(), aboutStr.c_str(), true);
  }
}

void MqttClient::reconnect() {
  if (lastConnectAttempt > 0 && (millis() - lastConnectAttempt) < MQTT_CONNECTION_ATTEMPT_FREQUENCY) {
    return;
  }

  if (! mqttClient.connected()) {
    if (connect()) {
      subscribe();
      sendBirthMessage();

#ifdef MQTT_DEBUG
      Serial.println(F("MqttClient - Successfully connected to MQTT server"));
#endif
    } else {
      Serial.print(F("ERROR: Failed to connect to MQTT server rc="));
      Serial.println(mqttClient.state());
    }
  }

  lastConnectAttempt = millis();
}

void MqttClient::handleClient() {
  reconnect();
  mqttClient.loop();

  if (!connected && mqttClient.connected()) {
    this->connected = true;
    this->onConnectFn();
  } else if (!mqttClient.connected()) {
    this->connected = false;
  }
}

void MqttClient::sendUpdate(const MiLightRemoteConfig& remoteConfig, uint16_t deviceId, uint16_t groupId, const char* update) {
  publish(settings.mqttUpdateTopicPattern, remoteConfig, deviceId, groupId, update, false);
}

void MqttClient::sendState(const MiLightRemoteConfig& remoteConfig, uint16_t deviceId, uint16_t groupId, const char* update) {
  publish(settings.mqttStateTopicPattern, remoteConfig, deviceId, groupId, update, true);
}

void MqttClient::subscribe() {
  String topic = settings.mqttTopicPattern;

  topic.replace(":device_id", "+");
  topic.replace(":hex_device_id", "+");
  topic.replace(":dec_device_id", "+");
  topic.replace(":group_id", "+");
  topic.replace(":device_type", "+");
  topic.replace(":device_alias", "+");

#ifdef MQTT_DEBUG
  printf_P(PSTR("MqttClient - subscribing to topic: %s\n"), topic.c_str());
#endif

  mqttClient.subscribe(topic.c_str());
}

void MqttClient::send(const char* topic, const char* message, const bool retain) {
  size_t len = strlen(message);
  size_t topicLen = strlen(topic);

  if ((topicLen + len + 10) < MQTT_MAX_PACKET_SIZE ) {
    mqttClient.publish(topic, message, retain);
  } else {
    const uint8_t* messageBuffer = reinterpret_cast<const uint8_t*>(message);
    mqttClient.beginPublish(topic, len, retain);

#ifdef MQTT_DEBUG
    Serial.printf_P(PSTR("Printing message in parts because it's too large for the packet buffer (%d bytes)"), len);
#endif

    for (size_t i = 0; i < len; i += MQTT_PACKET_CHUNK_SIZE) {
      size_t toWrite = std::min(static_cast<size_t>(MQTT_PACKET_CHUNK_SIZE), len - i);
      mqttClient.write(messageBuffer+i, toWrite);
#ifdef MQTT_DEBUG
      Serial.printf_P(PSTR("  Wrote %d bytes\n"), toWrite);
#endif
    }

    mqttClient.endPublish();
  }
}

void MqttClient::publish(
  const String& _topic,
  const MiLightRemoteConfig &remoteConfig,
  uint16_t deviceId,
  uint16_t groupId,
  const char* message,
  const bool _retain
) {
  if (_topic.length() == 0) {
    return;
  }

  BulbId bulbId(deviceId, groupId, remoteConfig.type);
  char topicBuf[MQTT_TOPIC_BUFFER_SIZE];
  bindTopicTo(_topic, bulbId, topicBuf, sizeof(topicBuf));
  const bool retain = _retain && this->settings.mqttRetain;

#ifdef MQTT_DEBUG
  printf("MqttClient - publishing update to %s\n", topicBuf);
#endif

  send(topicBuf, message, retain);
}

void MqttClient::publishCallback(char* topic, byte* payload, int length) {
  uint16_t deviceId = 0;
  uint8_t groupId = 0;
  const MiLightRemoteConfig* config = &FUT092Config;
  char cstrPayload[length + 1];
  cstrPayload[length] = 0;
  memcpy(cstrPayload, payload, sizeof(byte)*length);

#ifdef MQTT_DEBUG
  printf("MqttClient - Got message on topic: %s\n%s\n", topic, cstrPayload);
#endif

  auto patternIterator = std::make_shared<TokenIterator>(settings.mqttTopicPattern.c_str(), settings.mqttTopicPattern.length(), '/');
  auto topicIterator = std::make_shared<TokenIterator>(topic, strlen(topic), '/');
  UrlTokenBindings tokenBindings(patternIterator, topicIterator);

  if (tokenBindings.hasBinding("device_alias")) {
    String alias = tokenBindings.get("device_alias");
    auto itr = settings.groupIdAliases.find(alias);

    if (itr == settings.groupIdAliases.end()) {
      Serial.printf_P(PSTR("MqttClient - WARNING: could not find device alias: `%s'. Ignoring packet.\n"), alias.c_str());
      return;
    } else {
      BulbId bulbId = itr->second.bulbId;

      deviceId = bulbId.deviceId;
      config = MiLightRemoteConfig::fromType(bulbId.deviceType);
      groupId = bulbId.groupId;
    }
  } else {
    if (tokenBindings.hasBinding(GroupStateFieldNames::DEVICE_ID)) {
      deviceId = parseInt<uint16_t>(tokenBindings.get(GroupStateFieldNames::DEVICE_ID));
    } else if (tokenBindings.hasBinding("hex_device_id")) {
      deviceId = parseInt<uint16_t>(tokenBindings.get("hex_device_id"));
    } else if (tokenBindings.hasBinding("dec_device_id")) {
      deviceId = parseInt<uint16_t>(tokenBindings.get("dec_device_id"));
    }

    if (tokenBindings.hasBinding(GroupStateFieldNames::GROUP_ID)) {
      groupId = parseInt<uint16_t>(tokenBindings.get(GroupStateFieldNames::GROUP_ID));
    }

    if (tokenBindings.hasBinding(GroupStateFieldNames::DEVICE_TYPE)) {
      config = MiLightRemoteConfig::fromType(tokenBindings.get(GroupStateFieldNames::DEVICE_TYPE));
    } else {
      Serial.println(F("MqttClient - WARNING: could not find device_type token.  Defaulting to FUT092.\n"));
    }
  }

  if (config == NULL) {
    Serial.println(F("MqttClient - ERROR: unknown device_type specified"));
    return;
  }

  StaticJsonDocument<400> buffer;
  deserializeJson(buffer, cstrPayload);
  JsonObject obj = buffer.as<JsonObject>();

#ifdef MQTT_DEBUG
  printf("MqttClient - device %04X, group %u\n", deviceId, groupId);
#endif

  milightClient->prepare(config, deviceId, groupId);
  milightClient->update(obj);
}

size_t MqttClient::bindTopicTo(const String& topicPattern, const BulbId& bulbId, char* out, size_t outSize) {
  if (outSize == 0) {
    return 0;
  }

  const char* p = topicPattern.c_str();
  char* w = out;
  char* const limit = out + outSize - 1;  // reserve one byte for null terminator

  // Resolve the alias lazily — most patterns won't reference it, and the
  // lookup is an O(n) scan over the alias map.
  const char* aliasCStr = nullptr;

  while (*p && w < limit) {
    if (*p != ':') {
      *w++ = *p++;
      continue;
    }

    size_t skip;
    char numBuf[8];

    if ((skip = matchToken(p, "hex_device_id", 13))) {
      int n = snprintf(numBuf, sizeof(numBuf), "%04X", bulbId.deviceId);
      appendBounded(w, limit, numBuf, n < 0 ? 0 : static_cast<size_t>(n));
      p += skip;
    } else if ((skip = matchToken(p, "dec_device_id", 13))) {
      int n = snprintf(numBuf, sizeof(numBuf), "%u", bulbId.deviceId);
      appendBounded(w, limit, numBuf, n < 0 ? 0 : static_cast<size_t>(n));
      p += skip;
    } else if ((skip = matchToken(p, "device_id", 9))) {
      int n = snprintf(numBuf, sizeof(numBuf), "%04X", bulbId.deviceId);
      appendBounded(w, limit, numBuf, n < 0 ? 0 : static_cast<size_t>(n));
      p += skip;
    } else if ((skip = matchToken(p, "group_id", 8))) {
      int n = snprintf(numBuf, sizeof(numBuf), "%u", bulbId.groupId);
      appendBounded(w, limit, numBuf, n < 0 ? 0 : static_cast<size_t>(n));
      p += skip;
    } else if ((skip = matchToken(p, "device_type", 11))) {
      // remoteTypeToString returns a String — one unavoidable allocation
      // per device_type token until that helper is migrated to const char*.
      String typeStr = MiLightRemoteTypeHelpers::remoteTypeToString(bulbId.deviceType);
      appendBounded(w, limit, typeStr.c_str(), typeStr.length());
      p += skip;
    } else if ((skip = matchToken(p, "device_alias", 12))) {
      if (aliasCStr == nullptr) {
        auto it = settings.findAlias(bulbId.deviceType, bulbId.deviceId, bulbId.groupId);
        aliasCStr = (it != settings.groupIdAliases.end())
          ? it->first.c_str()
          : "__unnamed_group";
      }
      appendBounded(w, limit, aliasCStr, strlen(aliasCStr));
      p += skip;
    } else {
      // Unknown ":foo" — pass through literally (matches old replace()
      // behavior, which simply left unrecognized tokens in place).
      *w++ = *p++;
    }
  }

  *w = '\0';
  return static_cast<size_t>(w - out);
}

String MqttClient::bindTopicString(const String& topicPattern, const BulbId& bulbId) {
  char buf[MQTT_TOPIC_BUFFER_SIZE];
  bindTopicTo(topicPattern, bulbId, buf, sizeof(buf));
  return String(buf);
}

String MqttClient::generateConnectionStatusMessage(const char* connectionStatus) {
  if (settings.simpleMqttClientStatus) {
    // Don't expand disconnect type for simple status
    if (0 == strcmp(connectionStatus, STATUS_CONNECTED)) {
      return connectionStatus;
    } else {
      return "disconnected";
    }
  } else {
    StaticJsonDocument<1024> json;
    json[GroupStateFieldNames::STATUS] = connectionStatus;

    // Fill other fields
    AboutHelper::generateAboutObject(json, true);

    String response;
    serializeJson(json, response);

    return response;
  }
}

bool MqttClient::isConnected() {
  return this->mqttClient.connected();
}

MqttConnectionStatus MqttClient::getConnectionStatus() {
  return static_cast<MqttConnectionStatus>(this->mqttClient.state());
}

const __FlashStringHelper* MqttClient::getConnectionStatusString() {
  return MQTT_STATUS_STRINGS.at(this->mqttClient.state());
}
