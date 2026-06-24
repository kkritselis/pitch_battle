#include "ipixel.h"

#if ENABLE_IPIXEL

#include <NimBLEDevice.h>
#include "esp_coexist.h"

#if IPIXEL_USE_STATIC_SCOREBOARD_TEST_PNG
#include "scoreboard_test_png.h"
#endif
#include "ipixel_scroll_text.h"

static const char *IPIXEL_WRITE_UUID = "0000fa02-0000-1000-8000-00805f9b34fb";
static const char *IPIXEL_NOTIFY_UUID = "0000fa03-0000-1000-8000-00805f9b34fb";
static const char *IPIXEL_SERVICE_UUID = "0000fa00-0000-1000-8000-00805f9b34fb";
static const char *IPIXEL_AE_WRITE_UUID = "0000ae01-0000-1000-8000-00805f9b34fb";
static const char *IPIXEL_AE_NOTIFY_UUID = "0000ae02-0000-1000-8000-00805f9b34fb";
static constexpr size_t IPIXEL_IMAGE_CHUNK_BYTES = 244;
static constexpr size_t IPIXEL_IMAGE_WINDOW_BYTES = 12 * 1024;
// PNG window header after the 2-byte length prefix: [02 00 option] + size(4) +
// crc32(4) + [00 saveSlot] = 13 bytes (matches pypixelcolor _build_send_plan).
static constexpr size_t IPIXEL_IMAGE_HEADER_BYTES = 13;
static constexpr size_t IPIXEL_IMAGE_MESSAGE_MAX_BYTES =
  2 + IPIXEL_IMAGE_HEADER_BYTES + SCOREBOARD_PNG_MAX_BYTES;

static NimBLEClient *ipixelClient = nullptr;
static NimBLERemoteCharacteristic *ipixelWriteChar = nullptr;
static std::string ipixelTargetAddress;
static bool ipixelHaveTargetAddress = false;
static bool ipixelReady = false;
static bool ipixelLogoShown = false;
static bool ipixelScanning = false;
static bool ipixelWifiActive = false;
static uint32_t ipixelNextAttemptMs = 0;
static uint32_t ipixelLastStatusPrintMs = 0;
static uint8_t ipixelDevicesSeenCount = 0;
static volatile bool ipixelAckPending = false;
static volatile bool ipixelAckReceived = false;
static bool ipixelBurstActive = false;
static uint8_t ipixelBurstSlot = 0;
static uint8_t ipixelBurstStep = 0;
static uint32_t ipixelBurstNextMs = 0;
static char ipixelBurstLabel[16] = "";
static bool ipixelBurstReturnToScoreboard = false;
static bool ipixelBurstIsText = false;
static const uint8_t *ipixelBurstTextData = nullptr;
static size_t ipixelBurstTextLength = 0;
static bool ipixelScoreboardPending = false;
static bool ipixelHaveScoreboardState = false;
static ScoreboardState ipixelScoreboardState;
static uint32_t ipixelScoreboardDueMs = 0;
static uint8_t ipixelScoreboardPng[SCOREBOARD_PNG_MAX_BYTES];
static uint8_t ipixelImageMessage[IPIXEL_IMAGE_MESSAGE_MAX_BYTES];

struct IPixelWriteResponseContext {
  TaskHandle_t task;
  int status;
};

static void ipixelStartSlotBurst(
  uint8_t slot,
  const String &label,
  bool returnToScoreboard
);
static void ipixelStartTextBurst(
  const String &label,
  const uint8_t *data,
  size_t length
);
static bool ipixelSendRawMessage(const uint8_t *message, size_t messageLength);
bool ipixelIsConnected();

static bool ipixelUseConfiguredMac() {
  return IPIXEL_MAC[0] != '\0';
}

static bool ipixelAltPrefixEnabled() {
  return IPIXEL_ALT_PREFIX[0] != '\0';
}

static std::string ipixelTrimName(const std::string &name) {
  const size_t end = name.find_last_not_of(" \t");
  if (end == std::string::npos) {
    return "";
  }
  return name.substr(0, end + 1);
}

static bool ipixelNameLooksLikeTarget(const std::string &name) {
  const std::string trimmed = ipixelTrimName(name);
  if (trimmed.empty()) {
    return false;
  }

  if (trimmed.rfind(IPIXEL_DEVICE_PREFIX, 0) == 0) {
    return true;
  }

  if (ipixelAltPrefixEnabled() && trimmed.rfind(IPIXEL_ALT_PREFIX, 0) == 0) {
    return true;
  }

  return false;
}

static bool ipixelHasTargetManufacturerData(NimBLEAdvertisedDevice *device) {
  if (!device->haveManufacturerData()) {
    return false;
  }

  std::string mfg = device->getManufacturerData();
  if (mfg.size() < 2) {
    return false;
  }

  const uint16_t companyId =
    (uint8_t)mfg[0] | ((uint16_t)(uint8_t)mfg[1] << 8);

  return companyId == IPIXEL_MANUFACTURER_ID;
}

static bool ipixelDeviceLooksLikeTarget(NimBLEAdvertisedDevice *device) {
  if (device->haveName() && ipixelNameLooksLikeTarget(device->getName())) {
    return true;
  }

  const std::string details = device->toString();
  if (details.find(IPIXEL_DEVICE_PREFIX) != std::string::npos) {
    return true;
  }

  return ipixelHasTargetManufacturerData(device);
}

static void ipixelLogNotify(uint8_t *data, size_t length) {
#if IPIXEL_DIAGNOSTIC_MODE
  const bool shouldLog = true;
#else
  const bool shouldLog = ipixelAckPending;
#endif

  if (!shouldLog) {
    return;
  }

  Serial.print("iPixel notify: ");
  for (size_t i = 0; i < length; i++) {
    if (data[i] < 16) Serial.print('0');
    Serial.print(data[i], HEX);
    Serial.print(' ');
  }
  Serial.println();
}

static void ipixelHandleNotify(
  NimBLERemoteCharacteristic *characteristic,
  uint8_t *data,
  size_t length,
  bool isNotify
) {
  ipixelLogNotify(data, length);

  if (length >= 5 && data[0] == 0x05 && (data[4] == 0x00 || data[4] == 0x01 || data[4] == 0x03)) {
    Serial.print("iPixel ACK code=");
    Serial.println(data[4]);
    ipixelAckReceived = true;
    ipixelAckPending = false;
  }
}

static bool ipixelWaitForAck(uint32_t timeoutMs) {
  const uint32_t start = millis();

  while (ipixelAckPending && (millis() - start) < timeoutMs) {
    delay(5);
  }

  return ipixelAckReceived;
}

static uint32_t ipixelCrc32(const uint8_t *data, size_t length) {
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1)));
    }
  }
  return ~crc;
}

static void ipixelWriteLE16(uint8_t *out, uint16_t value) {
  out[0] = value & 0xFF;
  out[1] = (value >> 8) & 0xFF;
}

static void ipixelWriteLE32(uint8_t *out, uint32_t value) {
  out[0] = value & 0xFF;
  out[1] = (value >> 8) & 0xFF;
  out[2] = (value >> 16) & 0xFF;
  out[3] = (value >> 24) & 0xFF;
}

static int ipixelOnWriteResponse(
  uint16_t connHandle,
  const struct ble_gatt_error *error,
  struct ble_gatt_attr *attr,
  void *arg
) {
  IPixelWriteResponseContext *ctx = (IPixelWriteResponseContext *)arg;
  ctx->status = error != nullptr ? error->status : -1;
  xTaskNotifyGive(ctx->task);
  return 0;
}

static bool ipixelWriteWithResponseDirect(
  uint16_t connId,
  uint16_t handle,
  const uint8_t *data,
  size_t length
) {
  IPixelWriteResponseContext ctx = {
    xTaskGetCurrentTaskHandle(),
    -999
  };

#ifdef ulTaskNotifyValueClear
  ulTaskNotifyValueClear(ctx.task, ULONG_MAX);
#endif

  const int startRc =
    ble_gattc_write_flat(connId, handle, data, length, ipixelOnWriteResponse, &ctx);
  Serial.print("iPixel direct response write startRc=");
  Serial.print(startRc);
  Serial.print(" connId=");
  Serial.print(connId);
  Serial.print(" handle=0x");
  Serial.print(handle, HEX);
  Serial.print(" len=");
  Serial.println(length);

  if (startRc != 0) {
    return false;
  }

  const uint32_t notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(3000));
  if (notified == 0) {
    Serial.println("iPixel direct response write timeout");
    return false;
  }

  Serial.print("iPixel direct response write status=");
  Serial.println(ctx.status);
  return ctx.status == 0 || ctx.status == BLE_HS_EDONE;
}

class IPixelScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice *advertisedDevice) override {
    const std::string name =
      advertisedDevice->haveName() ? ipixelTrimName(advertisedDevice->getName()) : "";

    Serial.print("BLE device: ");
    Serial.print(name.empty() ? "(no name)" : name.c_str());
    Serial.print(" ");
    Serial.println(advertisedDevice->getAddress().toString().c_str());
    Serial.flush();

    ipixelDevicesSeenCount++;

    if (ipixelLogoShown || !ipixelDeviceLooksLikeTarget(advertisedDevice)) {
      return;
    }

    Serial.print("iPixel candidate found: ");
    Serial.println(advertisedDevice->toString().c_str());

    NimBLEDevice::getScan()->stop();
    ipixelTargetAddress = advertisedDevice->getAddress().toString();
    ipixelHaveTargetAddress = true;
  }
};

class IPixelClientCallbacks : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient *client) override {
    Serial.println("iPixel BLE link established");
  }

  void onDisconnect(NimBLEClient *client) override {
    Serial.println("iPixel BLE disconnected");
    ipixelWriteChar = nullptr;
    ipixelReady = false;
  }
};

static IPixelScanCallbacks ipixelScanCallbacks;
static IPixelClientCallbacks ipixelClientCallbacks;

static void ipixelDumpGatt(NimBLEClient *client) {
#if IPIXEL_LOG_GATT
  std::vector<NimBLERemoteService *> *services = client->getServices(true);
  if (services == nullptr || services->empty()) {
    Serial.println("iPixel GATT: no services discovered");
    return;
  }

  Serial.print("iPixel GATT: ");
  Serial.print(services->size());
  Serial.println(" service(s)");

  for (NimBLERemoteService *service : *services) {
    Serial.print("  service ");
    Serial.println(service->getUUID().toString().c_str());

    std::vector<NimBLERemoteCharacteristic *> *chars =
      service->getCharacteristics(true);
    if (chars == nullptr) {
      continue;
    }

    for (NimBLERemoteCharacteristic *c : *chars) {
      Serial.print("    char ");
      Serial.print(c->getUUID().toString().c_str());
      Serial.print(" handle=0x");
      Serial.print(c->getHandle(), HEX);
      Serial.print("  props:");
      if (c->canRead()) Serial.print(" read");
      if (c->canWrite()) Serial.print(" write");
      if (c->canWriteNoResponse()) Serial.print(" writeNR");
      if (c->canNotify()) Serial.print(" notify");
      if (c->canIndicate()) Serial.print(" indicate");
      Serial.println();
    }
  }
#else
  client->getServices(true);
#endif
}

static NimBLERemoteCharacteristic *ipixelFindCharacteristic(
  NimBLEClient *client,
  const char *uuid
) {
  std::vector<NimBLERemoteService *> *services = client->getServices(true);
  if (services == nullptr) {
    return nullptr;
  }

  for (NimBLERemoteService *remoteService : *services) {
    NimBLERemoteCharacteristic *characteristic =
      remoteService->getCharacteristic(uuid);
    if (characteristic != nullptr) {
      return characteristic;
    }
  }

  return nullptr;
}

// Fallback: first characteristic that supports writing.
static NimBLERemoteCharacteristic *ipixelFindWritable(NimBLEClient *client) {
  std::vector<NimBLERemoteService *> *services = client->getServices(true);
  if (services == nullptr) {
    return nullptr;
  }

  for (NimBLERemoteService *service : *services) {
    std::vector<NimBLERemoteCharacteristic *> *chars =
      service->getCharacteristics(true);
    if (chars == nullptr) {
      continue;
    }
    for (NimBLERemoteCharacteristic *c : *chars) {
      if (c->canWrite() || c->canWriteNoResponse()) {
        return c;
      }
    }
  }

  return nullptr;
}

// Fallback: first characteristic that supports notifications.
static NimBLERemoteCharacteristic *ipixelFindNotifiable(NimBLEClient *client) {
  std::vector<NimBLERemoteService *> *services = client->getServices(true);
  if (services == nullptr) {
    return nullptr;
  }

  for (NimBLERemoteService *service : *services) {
    std::vector<NimBLERemoteCharacteristic *> *chars =
      service->getCharacteristics(true);
    if (chars == nullptr) {
      continue;
    }
    for (NimBLERemoteCharacteristic *c : *chars) {
      if (c->canNotify() || c->canIndicate()) {
        return c;
      }
    }
  }

  return nullptr;
}

static bool ipixelSendCommand(const uint8_t *data, size_t length, bool waitForAck) {
  if (!ipixelReady || ipixelWriteChar == nullptr) {
    return false;
  }

  Serial.print("iPixel write: ");
  for (size_t i = 0; i < length; i++) {
    if (data[i] < 16) Serial.print('0');
    Serial.print(data[i], HEX);
    Serial.print(' ');
  }
  Serial.println();

  ipixelAckReceived = false;
  ipixelAckPending = waitForAck;

#if IPIXEL_WRITE_WITH_RESPONSE
  const uint16_t connId = ipixelClient->getConnId();
  const uint16_t handle =
    IPIXEL_RAW_WRITE_HANDLE != 0 ? IPIXEL_RAW_WRITE_HANDLE : ipixelWriteChar->getHandle();
  const bool ok = ipixelWriteWithResponseDirect(connId, handle, data, length);
  Serial.print("iPixel write result: response=");
  Serial.print(ok ? "ok" : "fail");
  Serial.print(" connId=");
  Serial.print(connId);
  Serial.print(" handle=0x");
  Serial.print(handle, HEX);
  Serial.print(" len=");
  Serial.println(length);
  if (!ok) {
    ipixelAckPending = false;
    return false;
  }
#else
  // The ESP32's current stable path is direct write-without-response. It avoids
  // the fa02 response-write failure seen on this device, but has no delivery
  // confirmation, so diagnostics can compare it against the Mac-style response
  // path by setting IPIXEL_WRITE_WITH_RESPONSE to 1.
  const uint16_t connId = ipixelClient->getConnId();
  const uint16_t handle =
    IPIXEL_RAW_WRITE_HANDLE != 0 ? IPIXEL_RAW_WRITE_HANDLE : ipixelWriteChar->getHandle();
  const int rc = ble_gattc_write_no_rsp_flat(connId, handle, data, length);
  Serial.print("iPixel write result: rc=");
  Serial.print(rc);
  Serial.print(" connId=");
  Serial.print(connId);
  Serial.print(" handle=0x");
  Serial.print(handle, HEX);
  Serial.print(" len=");
  Serial.println(length);
  if (rc != 0) {
    ipixelAckPending = false;
    return false;
  }
#endif

  if (!waitForAck) {
    return true;
  }

  if (!ipixelWaitForAck(3000)) {
    Serial.println("iPixel command ack timeout");
    return false;
  }

  return true;
}

static bool ipixelSendCommandToHandle(
  uint16_t handle,
  const uint8_t *data,
  size_t length,
  bool waitForAck
) {
  if (!ipixelReady || ipixelClient == nullptr || !ipixelClient->isConnected()) {
    return false;
  }

  Serial.print("iPixel raw handle write: handle=0x");
  Serial.print(handle, HEX);
  Serial.print(" data=");
  for (size_t i = 0; i < length; i++) {
    if (data[i] < 16) Serial.print('0');
    Serial.print(data[i], HEX);
    Serial.print(' ');
  }
  Serial.println();

  ipixelAckReceived = false;
  ipixelAckPending = waitForAck;

  const uint16_t connId = ipixelClient->getConnId();
#if IPIXEL_WRITE_WITH_RESPONSE
  const bool ok = ipixelWriteWithResponseDirect(connId, handle, data, length);
  Serial.print("iPixel raw handle response result=");
  Serial.print(ok ? "ok" : "fail");
  Serial.print(" connId=");
  Serial.print(connId);
  Serial.print(" handle=0x");
  Serial.print(handle, HEX);
  Serial.print(" len=");
  Serial.println(length);
  if (!ok) {
    ipixelAckPending = false;
    return false;
  }
#else
  const int rc = ble_gattc_write_no_rsp_flat(connId, handle, data, length);
  Serial.print("iPixel raw handle result: rc=");
  Serial.print(rc);
  Serial.print(" connId=");
  Serial.print(connId);
  Serial.print(" handle=0x");
  Serial.print(handle, HEX);
  Serial.print(" len=");
  Serial.println(length);

  if (rc != 0) {
    ipixelAckPending = false;
    return false;
  }
#endif

  if (!waitForAck) {
    return true;
  }

  if (!ipixelWaitForAck(2000)) {
    Serial.println("iPixel raw handle ack timeout");
    return false;
  }

  return true;
}

static bool ipixelSendDeviceInfo() {
  // This display does not return a notify ACK for the handshake, so fire and
  // forget; a short delay below lets the firmware process it.
  const uint8_t cmd[] = {0x08, 0x00, 0x01, 0x80, 12, 0, 0, 0x00};
  return ipixelSendCommand(cmd, sizeof(cmd), false);
}

static bool ipixelSendSlotCommand(uint8_t slot) {
  // Matches pypixelcolor show_slot(): [0x07,0x00,0x08,0x80,0x01,0x00,slot].
  // Note: if the requested slot is empty the device falls back to cycling
  // through whatever slots are populated.
  const uint8_t cmd[] = {0x07, 0x00, 0x08, 0x80, 0x01, 0x00, slot};
  return ipixelSendCommand(cmd, sizeof(cmd), IPIXEL_REQUIRE_SLOT_ACK);
}

static bool ipixelSendRawMessage(const uint8_t *message, size_t messageLength) {
  if (!ipixelIsConnected() || ipixelWriteChar == nullptr || message == nullptr || messageLength == 0) {
    return false;
  }

  if (messageLength > IPIXEL_IMAGE_MESSAGE_MAX_BYTES) {
    Serial.println("iPixel raw message too large");
    return false;
  }

  const uint16_t connId = ipixelClient->getConnId();
  const uint16_t handle =
    IPIXEL_RAW_WRITE_HANDLE != 0 ? IPIXEL_RAW_WRITE_HANDLE : ipixelWriteChar->getHandle();

  Serial.print("iPixel raw message send bytes=");
  Serial.print(messageLength);
  Serial.print(" handle=0x");
  Serial.println(handle, HEX);

  ipixelAckReceived = false;
  ipixelAckPending = true;

  for (size_t pos = 0; pos < messageLength; pos += IPIXEL_IMAGE_CHUNK_BYTES) {
    const size_t chunkLength =
      min(IPIXEL_IMAGE_CHUNK_BYTES, messageLength - pos);
    if (!ipixelWriteWithResponseDirect(
      connId,
      handle,
      message + pos,
      chunkLength
    )) {
      Serial.println("iPixel raw message chunk write failed");
      ipixelAckPending = false;
      return false;
    }
    delay(4);
  }

  if (!ipixelWaitForAck(IPIXEL_REQUIRE_IMAGE_ACK ? 8000 : 1500)) {
    ipixelAckPending = false;
#if IPIXEL_REQUIRE_IMAGE_ACK
    Serial.println("iPixel raw message ack timeout");
    return false;
#else
    Serial.println("iPixel raw message sent (no ACK; assuming displayed)");
    return true;
#endif
  }

  Serial.println("iPixel raw message ACK received");
  return true;
}

static bool ipixelSendImageBytes(
  const uint8_t *fileBytes,
  size_t fileLength,
  uint8_t saveSlot,
  uint16_t handle
) {
  if (!ipixelIsConnected() || ipixelWriteChar == nullptr || fileBytes == nullptr || fileLength == 0) {
    return false;
  }

  if (fileLength > IPIXEL_IMAGE_WINDOW_BYTES) {
    Serial.println("iPixel live image too large for single-window sender");
    return false;
  }

  const size_t frameLength = IPIXEL_IMAGE_HEADER_BYTES + fileLength;
  const size_t messageLength = 2 + frameLength;
  if (messageLength > sizeof(ipixelImageMessage) || messageLength > 0xFFFF) {
    Serial.println("iPixel live image frame does not fit buffer");
    return false;
  }

  ipixelWriteLE16(ipixelImageMessage, (uint16_t)messageLength);
  ipixelImageMessage[2] = 0x02;
  ipixelImageMessage[3] = 0x00;
  ipixelImageMessage[4] = 0x00;
  ipixelWriteLE32(ipixelImageMessage + 5, (uint32_t)fileLength);
  ipixelWriteLE32(ipixelImageMessage + 9, ipixelCrc32(fileBytes, fileLength));
  ipixelImageMessage[13] = 0x00;
  ipixelImageMessage[14] = saveSlot;
  memcpy(ipixelImageMessage + 15, fileBytes, fileLength);

  const uint16_t connId = ipixelClient->getConnId();

  Serial.print("iPixel live image send bytes=");
  Serial.print(fileLength);
  Serial.print(" frame=");
  Serial.print(messageLength);
  Serial.print(" saveSlot=");
  Serial.print(saveSlot);
  Serial.print(" handle=0x");
  Serial.println(handle, HEX);

  ipixelAckReceived = false;
  ipixelAckPending = true;

  for (size_t pos = 0; pos < messageLength; pos += IPIXEL_IMAGE_CHUNK_BYTES) {
    const size_t chunkLength =
      min(IPIXEL_IMAGE_CHUNK_BYTES, messageLength - pos);
    if (!ipixelWriteWithResponseDirect(
      connId,
      handle,
      ipixelImageMessage + pos,
      chunkLength
    )) {
      Serial.println("iPixel live image chunk write failed");
      ipixelAckPending = false;
      return false;
    }
    delay(4);
  }

  if (!ipixelWaitForAck(IPIXEL_REQUIRE_IMAGE_ACK ? 8000 : 1500)) {
    ipixelAckPending = false;
#if IPIXEL_REQUIRE_IMAGE_ACK
    Serial.println("iPixel live image ack timeout");
    return false;
#else
    Serial.println("iPixel live image sent (no ACK; assuming displayed)");
    return true;
#endif
  }

  Serial.println("iPixel live image ACK received");
  return true;
}

static bool ipixelSendImageBytesViaCharacteristic(
  const uint8_t *fileBytes,
  size_t fileLength,
  uint8_t saveSlot
) {
  if (!ipixelIsConnected() || ipixelWriteChar == nullptr || fileBytes == nullptr || fileLength == 0) {
    return false;
  }

  if (fileLength > IPIXEL_IMAGE_WINDOW_BYTES) {
    Serial.println("iPixel live image too large for characteristic sender");
    return false;
  }

  const size_t frameLength = IPIXEL_IMAGE_HEADER_BYTES + fileLength;
  const size_t messageLength = 2 + frameLength;
  if (messageLength > sizeof(ipixelImageMessage) || messageLength > 0xFFFF) {
    Serial.println("iPixel characteristic image frame does not fit buffer");
    return false;
  }

  ipixelWriteLE16(ipixelImageMessage, (uint16_t)messageLength);
  ipixelImageMessage[2] = 0x02;
  ipixelImageMessage[3] = 0x00;
  ipixelImageMessage[4] = 0x00;
  ipixelWriteLE32(ipixelImageMessage + 5, (uint32_t)fileLength);
  ipixelWriteLE32(ipixelImageMessage + 9, ipixelCrc32(fileBytes, fileLength));
  ipixelImageMessage[13] = 0x00;
  ipixelImageMessage[14] = saveSlot;
  memcpy(ipixelImageMessage + 15, fileBytes, fileLength);

  Serial.print("iPixel live image char send bytes=");
  Serial.print(fileLength);
  Serial.print(" frame=");
  Serial.print(messageLength);
  Serial.print(" saveSlot=");
  Serial.print(saveSlot);
  Serial.print(" charHandle=0x");
  Serial.println(ipixelWriteChar->getHandle(), HEX);

  ipixelAckReceived = false;
  ipixelAckPending = true;

  for (size_t pos = 0; pos < messageLength; pos += IPIXEL_IMAGE_CHUNK_BYTES) {
    const size_t chunkLength =
      min(IPIXEL_IMAGE_CHUNK_BYTES, messageLength - pos);
    if (!ipixelWriteChar->writeValue(ipixelImageMessage + pos, chunkLength, true)) {
      Serial.print("iPixel live image char chunk failed at pos=");
      Serial.println(pos);
      ipixelAckPending = false;
      return false;
    }
    delay(4);
  }

  if (!ipixelWaitForAck(IPIXEL_REQUIRE_IMAGE_ACK ? 8000 : 1500)) {
    ipixelAckPending = false;
#if IPIXEL_REQUIRE_IMAGE_ACK
    Serial.println("iPixel live image char ack timeout");
    return false;
#else
    Serial.println("iPixel live image char sent (no ACK; assuming displayed)");
    return true;
#endif
  }

  Serial.println("iPixel live image char ACK received");
  return true;
}

static bool ipixelSendImageBytes(
  const uint8_t *fileBytes,
  size_t fileLength,
  uint8_t saveSlot
) {
  const uint16_t commandHandle =
    IPIXEL_RAW_WRITE_HANDLE != 0 ? IPIXEL_RAW_WRITE_HANDLE : ipixelWriteChar->getHandle();
  const uint16_t imageHandle =
    IPIXEL_IMAGE_RAW_WRITE_HANDLE != 0 ? IPIXEL_IMAGE_RAW_WRITE_HANDLE : commandHandle;

  if (ipixelSendImageBytes(fileBytes, fileLength, saveSlot, imageHandle)) {
    return true;
  }

  if (imageHandle != commandHandle) {
    Serial.print("iPixel image handle 0x");
    Serial.print(imageHandle, HEX);
    Serial.print(" failed; trying command handle 0x");
    Serial.println(commandHandle, HEX);
    if (ipixelSendImageBytes(fileBytes, fileLength, saveSlot, commandHandle)) {
      return true;
    }
  }

  Serial.println("iPixel raw image path failed; trying characteristic");
  return ipixelSendImageBytesViaCharacteristic(fileBytes, fileLength, saveSlot);
}

static bool ipixelPushScoreboardImage() {
  if (!ipixelHaveScoreboardState) {
    Serial.println("iPixel live scoreboard skipped (no state)");
    return false;
  }

  const size_t pngLength = renderScoreboardPng(
    ipixelScoreboardState,
    ipixelScoreboardPng,
    sizeof(ipixelScoreboardPng)
  );
  if (pngLength == 0) {
    Serial.println("iPixel live scoreboard render failed");
    return false;
  }

#if IPIXEL_USE_STATIC_SCOREBOARD_TEST_PNG
  const uint8_t *scoreboardBytes = SCOREBOARD_TEST_PNG;
  const size_t scoreboardLength = SCOREBOARD_TEST_PNG_BYTES;
  Serial.print("iPixel static scoreboard PNG test bytes=");
  Serial.println(scoreboardLength);
#else
  const uint8_t *scoreboardBytes = ipixelScoreboardPng;
  const size_t scoreboardLength = pngLength;
#endif

  if (ipixelWifiActive) {
    esp_coex_preference_set(ESP_COEX_PREFER_BT);
    Serial.println("iPixel coex prefer BT for live scoreboard");
  }

  Serial.println("iPixel live scoreboard handshake");
  if (!ipixelSendDeviceInfo()) {
    Serial.println("iPixel live scoreboard handshake failed");
  }
  delay(300);

  bool startedSlotBurst = false;
  const uint8_t scoreboardSlot = IPIXEL_SLOT_SCOREBOARD;
  bool sent = ipixelSendImageBytes(scoreboardBytes, scoreboardLength, scoreboardSlot);
  if (sent) {
    delay(300);
    if (!ipixelSendSlotCommand(scoreboardSlot)) {
      Serial.println("iPixel live scoreboard show_slot failed");
      sent = false;
    } else {
      Serial.print("iPixel live scoreboard displayed on slot ");
      Serial.println(scoreboardSlot);
    }
  } else {
    Serial.println("iPixel live scoreboard image push failed");
  }

  if (ipixelWifiActive && !startedSlotBurst) {
    esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);
    Serial.println("iPixel coex restored balance");
  }

  return sent;
}

static bool ipixelFinishConnection() {
  ipixelDumpGatt(ipixelClient);

  const char *writeUuid = IPIXEL_USE_AE_CHANNEL ? IPIXEL_AE_WRITE_UUID : IPIXEL_WRITE_UUID;
  const char *notifyUuid = IPIXEL_USE_AE_CHANNEL ? IPIXEL_AE_NOTIFY_UUID : IPIXEL_NOTIFY_UUID;

  Serial.print("iPixel channel: ");
  Serial.println(IPIXEL_USE_AE_CHANNEL ? "ae01/ae02" : "fa02/fa03");

  ipixelWriteChar = ipixelFindCharacteristic(ipixelClient, writeUuid);
  if (ipixelWriteChar == nullptr) {
    Serial.println("iPixel configured write char not found; trying first writable");
    ipixelWriteChar = ipixelFindWritable(ipixelClient);
  }
  if (ipixelWriteChar == nullptr) {
    Serial.println("iPixel write characteristic not found");
    ipixelClient->disconnect();
    return false;
  }
  Serial.print("iPixel using write char ");
  Serial.print(ipixelWriteChar->getUUID().toString().c_str());
  Serial.print(" handle=0x");
  Serial.println(ipixelWriteChar->getHandle(), HEX);

  NimBLERemoteCharacteristic *notifyChar =
    ipixelFindCharacteristic(ipixelClient, notifyUuid);
  if (notifyChar == nullptr) {
    notifyChar = ipixelFindNotifiable(ipixelClient);
  }
  if (notifyChar != nullptr && (notifyChar->canNotify() || notifyChar->canIndicate())) {
    if (!notifyChar->subscribe(true, ipixelHandleNotify)) {
      Serial.println("iPixel notify subscribe failed");
    }
  } else {
    Serial.println("iPixel notify characteristic missing");
  }

  ipixelReady = true;
  return true;
}

static bool ipixelConnectToAddress(const std::string &address) {
  if (address.empty()) {
    return false;
  }

  if (ipixelClient == nullptr) {
    ipixelClient = NimBLEDevice::createClient();
    ipixelClient->setClientCallbacks(&ipixelClientCallbacks, false);
    ipixelClient->setConnectTimeout(IPIXEL_SCAN_SECONDS);
  }

  if (ipixelClient->isConnected()) {
    return ipixelReady;
  }

  Serial.print("iPixel connecting to ");
  Serial.println(address.c_str());

  if (!ipixelClient->connect(NimBLEAddress(address))) {
    Serial.println("iPixel connect failed");
    return false;
  }

  return ipixelFinishConnection();
}

static bool ipixelShowLogoOnce() {
  if (ipixelLogoShown || !ipixelReady) {
    return ipixelLogoShown;
  }

  Serial.println("iPixel running boot sequence");

  if (!ipixelSendDeviceInfo()) {
    Serial.println("iPixel device info handshake failed");
    return false;
  }

  delay(300);

  if (!ipixelShowSlot(IPIXEL_SLOT_LOGO)) {
    Serial.println("iPixel logo slot failed");
    return false;
  }

  delay(300);

  Serial.println("iPixel logo slot sent");
  ipixelLogoShown = true;
  return true;
}

static bool ipixelScanOnce(bool allowDuringWifi) {
  if (ipixelWifiActive && !allowDuringWifi) {
    return false;
  }

  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(&ipixelScanCallbacks);
  scan->setActiveScan(true);
  scan->setInterval(45);
  scan->setWindow(44);
  scan->setDuplicateFilter(false);

  Serial.println("iPixel scanning...");
  ipixelScanning = true;
  scan->start(IPIXEL_SCAN_SECONDS, true);
  ipixelScanning = false;

  return ipixelHaveTargetAddress;
}

static bool ipixelTryConnectAndShowLogo(bool allowScanDuringWifi) {
  if (ipixelUseConfiguredMac()) {
    ipixelTargetAddress = IPIXEL_MAC;
    ipixelHaveTargetAddress = true;
  }

  if (!ipixelHaveTargetAddress) {
    if (!ipixelScanOnce(allowScanDuringWifi)) {
      return false;
    }
  }

  if (!ipixelConnectToAddress(ipixelTargetAddress)) {
    return false;
  }

  return ipixelShowLogoOnce();
}

static bool ipixelTryConnectOnly(bool allowScanDuringWifi) {
  if (ipixelUseConfiguredMac()) {
    ipixelTargetAddress = IPIXEL_MAC;
    ipixelHaveTargetAddress = true;
  }

  if (!ipixelHaveTargetAddress) {
    if (!ipixelScanOnce(allowScanDuringWifi)) {
      return false;
    }
  }

  return ipixelConnectToAddress(ipixelTargetAddress);
}

void ipixelBegin() {
  ipixelDevicesSeenCount = 0;

  Serial.println();
  Serial.println("iPixel BLE starting (before Wi-Fi)");
  Serial.flush();

  esp_coex_preference_set(ESP_COEX_PREFER_BT);
  NimBLEDevice::init("PitchBattle");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

#if IPIXEL_DIAGNOSTIC_MODE
  if (ipixelTryConnectOnly(false)) {
    Serial.print("iPixel diagnostic connect success. Saved address: ");
    Serial.println(ipixelTargetAddress.c_str());
  } else {
    Serial.println("iPixel diagnostic connect failed. Diagnostic loop will retry manually after reset.");
  }
  Serial.flush();
  return;
#endif

  const uint32_t deadline = millis() + IPIXEL_BOOT_WAIT_MS;

  while (!ipixelLogoShown && millis() < deadline) {
    if (ipixelTryConnectAndShowLogo(false)) {
      break;
    }

    Serial.println("iPixel waiting for display...");
    delay(2000);
  }

  if (ipixelLogoShown) {
    Serial.print("iPixel boot success. Saved address: ");
    Serial.println(ipixelTargetAddress.c_str());
  } else {
    Serial.println("iPixel boot timeout. Wi-Fi will start without logo.");
  }

  Serial.flush();
}

void ipixelNotifyWifiActive() {
  ipixelWifiActive = true;
  esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);
  ipixelNextAttemptMs = millis() + IPIXEL_BACKGROUND_RETRY_MS;
  Serial.println("iPixel: Wi-Fi active. Will keep scanning until logo is shown.");
  Serial.flush();
}

void ipixelLoop() {
  if (ipixelBurstActive && millis() >= ipixelBurstNextMs) {
    bool sent = false;

    if (!ipixelIsConnected()) {
      Serial.println("iPixel burst aborted (disconnected)");
      ipixelBurstActive = false;
    } else if (ipixelBurstStep == 0) {
      Serial.print("iPixel burst step handshake label=");
      Serial.println(ipixelBurstLabel);
      sent = ipixelSendDeviceInfo();
      ipixelBurstStep++;
      ipixelBurstNextMs = millis() + 120;
    } else if (ipixelBurstIsText && ipixelBurstStep == 1) {
      Serial.print("iPixel burst step scroll text label=");
      Serial.println(ipixelBurstLabel);
      delay(300);
      sent = ipixelSendRawMessage(ipixelBurstTextData, ipixelBurstTextLength);
      ipixelBurstStep = 4;
      ipixelBurstNextMs = millis() + 120;
    } else if (!ipixelBurstIsText && ipixelBurstStep <= 3) {
      Serial.print("iPixel burst step slot attempt=");
      Serial.print(ipixelBurstStep);
      Serial.print(" slot=");
      Serial.println(ipixelBurstSlot);
      sent = ipixelSendSlotCommand(ipixelBurstSlot);
      ipixelBurstStep++;
      ipixelBurstNextMs = millis() + 120;
    } else {
      Serial.print("iPixel burst complete label=");
      Serial.print(ipixelBurstLabel);
      if (ipixelBurstIsText) {
        Serial.println(" (scroll text)");
      } else {
        Serial.print(" slot=");
        Serial.println(ipixelBurstSlot);
      }
      if (ipixelBurstReturnToScoreboard &&
          (ipixelBurstIsText || ipixelBurstSlot != IPIXEL_SLOT_SCOREBOARD)) {
        ipixelScoreboardPending = true;
        ipixelScoreboardDueMs = millis() + IPIXEL_SCOREBOARD_RETURN_MS;
        Serial.print("iPixel scoreboard return scheduled in ms=");
        Serial.println(IPIXEL_SCOREBOARD_RETURN_MS);
      }
      ipixelBurstActive = false;
      ipixelBurstReturnToScoreboard = false;
      ipixelBurstIsText = false;
      ipixelBurstTextData = nullptr;
      ipixelBurstTextLength = 0;
      if (ipixelWifiActive) {
        esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);
        Serial.println("iPixel coex restored balance");
      }
    }

    if (ipixelBurstActive && !sent) {
      Serial.println("iPixel burst write failed; will continue retries");
    }
  }

  if (!ipixelBurstActive && ipixelScoreboardPending && millis() >= ipixelScoreboardDueMs) {
    ipixelScoreboardPending = false;
    Serial.println("iPixel returning to live scoreboard");
    if (!ipixelPushScoreboardImage()) {
      Serial.println("iPixel live scoreboard failed");
#if IPIXEL_SLOT_SCOREBOARD
      ipixelStartSlotBurst(IPIXEL_SLOT_SCOREBOARD, "score-fallback", false);
#endif
    }
  }

#if IPIXEL_DIAGNOSTIC_MODE
  return;
#endif

  if (!ipixelLogoShown && millis() - ipixelLastStatusPrintMs >= 10000) {
    ipixelLastStatusPrintMs = millis();
    Serial.print("iPixel status: logo=");
    Serial.print(ipixelLogoShown ? "yes" : "no");
    Serial.print(" bleDevicesSeen=");
    Serial.print(ipixelDevicesSeenCount);
    Serial.print(" haveAddress=");
    Serial.println(ipixelHaveTargetAddress ? "yes" : "no");
    Serial.flush();
  }

  if (ipixelBurstActive || ipixelLogoShown || ipixelScanning || millis() < ipixelNextAttemptMs) {
    return;
  }

  ipixelNextAttemptMs = millis() + IPIXEL_BACKGROUND_RETRY_MS;
  Serial.println("iPixel background retry...");

  if (ipixelTryConnectAndShowLogo(true)) {
    Serial.println("iPixel logo shown on background retry");
  }
}

bool ipixelLogoDisplayed() {
  return ipixelLogoShown;
}

uint8_t ipixelDevicesSeen() {
  return ipixelDevicesSeenCount;
}

const char *ipixelAddress() {
  return ipixelHaveTargetAddress ? ipixelTargetAddress.c_str() : "";
}

bool ipixelIsConnected() {
  return ipixelReady && ipixelClient != nullptr && ipixelClient->isConnected();
}

static uint8_t ipixelSlotForImage(const String &imageName) {
  if (imageName == "homerun") return IPIXEL_SLOT_HOMERUN;
  if (imageName == "triple") return IPIXEL_SLOT_TRIPLE;
  if (imageName == "double") return IPIXEL_SLOT_DOUBLE;
  if (imageName == "single") return IPIXEL_SLOT_SINGLE;
  if (imageName == "ball") return IPIXEL_SLOT_BALL;
  if (imageName == "strike") return IPIXEL_SLOT_BALL;
  if (imageName == "foul") return IPIXEL_SLOT_FOUL;
  if (imageName == "flyout" || imageName == "out") return IPIXEL_SLOT_FLYOUT;
  if (imageName == "groundout") return IPIXEL_SLOT_GROUNDOUT;
  return IPIXEL_SLOT_LOGO;
}

static void ipixelStartTextBurst(
  const String &label,
  const uint8_t *data,
  size_t length
) {
  if (!ipixelIsConnected() || data == nullptr || length == 0) {
    Serial.println("iPixel text burst skipped (not connected or empty payload)");
    return;
  }

  ipixelBurstIsText = true;
  ipixelBurstTextData = data;
  ipixelBurstTextLength = length;
  ipixelBurstSlot = 0;
  ipixelBurstStep = 0;
  ipixelBurstNextMs = millis();
  ipixelBurstActive = true;
  ipixelBurstReturnToScoreboard = true;
  ipixelScoreboardPending = false;
  label.toCharArray(ipixelBurstLabel, sizeof(ipixelBurstLabel));

  Serial.print("iPixel text burst queued label=");
  Serial.print(ipixelBurstLabel);
  Serial.print(" bytes=");
  Serial.println(length);

  if (ipixelWifiActive) {
    esp_coex_preference_set(ESP_COEX_PREFER_BT);
    Serial.println("iPixel coex prefer BT for text burst");
  }
}

static void ipixelStartSlotBurst(
  uint8_t slot,
  const String &label,
  bool returnToScoreboard
) {
  if (!ipixelIsConnected()) {
    Serial.println("iPixel burst skipped (not connected)");
    return;
  }

  ipixelBurstSlot = slot;
  ipixelBurstStep = 0;
  ipixelBurstNextMs = millis();
  ipixelBurstActive = true;
  ipixelBurstIsText = false;
  ipixelBurstTextData = nullptr;
  ipixelBurstTextLength = 0;
  ipixelBurstReturnToScoreboard = returnToScoreboard;
  ipixelScoreboardPending = false;
  label.toCharArray(ipixelBurstLabel, sizeof(ipixelBurstLabel));

  Serial.print("iPixel burst queued label=");
  Serial.print(ipixelBurstLabel);
  Serial.print(" slot=");
  Serial.println(ipixelBurstSlot);

  if (ipixelWifiActive) {
    esp_coex_preference_set(ESP_COEX_PREFER_BT);
    Serial.println("iPixel coex prefer BT for burst");
  }
}

bool ipixelShowSlot(uint8_t slot) {
  if (!ipixelIsConnected()) {
    return false;
  }

  return ipixelSendSlotCommand(slot);
}

bool ipixelShowSlotAtHandle(uint8_t slot, uint16_t handle, bool waitForAck) {
  const uint8_t cmd[] = {0x07, 0x00, 0x08, 0x80, 0x01, 0x00, slot};
  return ipixelSendCommandToHandle(handle, cmd, sizeof(cmd), waitForAck);
}

bool ipixelBusy() {
  return ipixelBurstActive || ipixelScoreboardPending;
}

void showIPixelScoreboard(const ScoreboardState &state) {
  if (!ipixelIsConnected()) {
    Serial.println("iPixel scoreboard skipped (not connected)");
    return;
  }

  ipixelScoreboardState = state;
  ipixelHaveScoreboardState = true;
  ipixelScoreboardPending = true;
  ipixelScoreboardDueMs = millis();
  Serial.println("iPixel live scoreboard queued");
}

void showIPixelResult(const String &imageName, const ScoreboardState &state) {
  if (!ipixelIsConnected()) {
    Serial.println("iPixel result skipped (not connected)");
    return;
  }

  ipixelScoreboardState = state;
  ipixelHaveScoreboardState = true;

  if (imageName == "walk") {
    Serial.println("iPixel result 'walk' -> scroll text");
    ipixelStartTextBurst("walk", IPIXEL_SCROLL_WALK, IPIXEL_SCROLL_WALK_BYTES);
    return;
  }

  if (imageName == "strikeout") {
    Serial.println("iPixel result 'strikeout' -> scroll text");
    ipixelStartTextBurst(
      "strikeout",
      IPIXEL_SCROLL_STRIKEOUT,
      IPIXEL_SCROLL_STRIKEOUT_BYTES
    );
    return;
  }

  const uint8_t slot = ipixelSlotForImage(imageName);
  Serial.print("iPixel result '");
  Serial.print(imageName);
  Serial.print("' -> slot ");
  Serial.println(slot);

  ipixelStartSlotBurst(slot, imageName, true);
}

#endif
