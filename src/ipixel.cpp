#include "ipixel.h"

#if ENABLE_IPIXEL

#include <NimBLEDevice.h>
#include "esp_coexist.h"

static const char *IPIXEL_WRITE_UUID = "0000fa02-0000-1000-8000-00805f9b34fb";
static const char *IPIXEL_NOTIFY_UUID = "0000fa03-0000-1000-8000-00805f9b34fb";
static const char *IPIXEL_SERVICE_UUID = "0000fa00-0000-1000-8000-00805f9b34fb";

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
      Serial.print("  props:");
      if (c->canRead()) Serial.print(" read");
      if (c->canWrite()) Serial.print(" write");
      if (c->canWriteNoResponse()) Serial.print(" writeNR");
      if (c->canNotify()) Serial.print(" notify");
      if (c->canIndicate()) Serial.print(" indicate");
      Serial.println();
    }
  }
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

  // iPixel firmware only accepts write-without-response on fa02; command ACKs
  // arrive separately over the fa03 notify channel. NimBLE's canWriteNoResponse()
  // is unreliable for this device, so call the host write-no-response directly.
  const uint16_t connId = ipixelClient->getConnId();
  const uint16_t handle = ipixelWriteChar->getHandle();
  const int rc = ble_gattc_write_no_rsp_flat(connId, handle, data, length);
  if (rc != 0) {
    ipixelAckPending = false;
    Serial.print("iPixel write failed (rc=");
    Serial.print(rc);
    Serial.println(")");
    return false;
  }

  if (!waitForAck) {
    return true;
  }

  if (!ipixelWaitForAck(3000)) {
    Serial.println("iPixel command ack timeout");
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
  const uint8_t cmd[] = {0x07, 0x00, 0x08, 0x80, 0x01, 0x00, slot};
  return ipixelSendCommand(cmd, sizeof(cmd), false);
}

static bool ipixelFinishConnection() {
  ipixelDumpGatt(ipixelClient);

  ipixelWriteChar = ipixelFindCharacteristic(ipixelClient, IPIXEL_WRITE_UUID);
  if (ipixelWriteChar == nullptr) {
    Serial.println("iPixel write char fa02 not found; trying first writable");
    ipixelWriteChar = ipixelFindWritable(ipixelClient);
  }
  if (ipixelWriteChar == nullptr) {
    Serial.println("iPixel write characteristic not found");
    ipixelClient->disconnect();
    return false;
  }
  Serial.print("iPixel using write char ");
  Serial.println(ipixelWriteChar->getUUID().toString().c_str());

  NimBLERemoteCharacteristic *notifyChar =
    ipixelFindCharacteristic(ipixelClient, IPIXEL_NOTIFY_UUID);
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

void ipixelBegin() {
  ipixelDevicesSeenCount = 0;

  Serial.println();
  Serial.println("iPixel BLE starting (before Wi-Fi)");
  Serial.flush();

  esp_coex_preference_set(ESP_COEX_PREFER_BT);
  NimBLEDevice::init("PitchBattle");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

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

  if (ipixelLogoShown || ipixelScanning || millis() < ipixelNextAttemptMs) {
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

bool ipixelShowSlot(uint8_t slot) {
  if (!ipixelIsConnected()) {
    return false;
  }

  return ipixelSendSlotCommand(slot);
}

void showIPixelResult(const String &imageName) {
  if (!ipixelIsConnected()) {
    Serial.println("iPixel result skipped (not connected)");
    return;
  }

  uint8_t slot = IPIXEL_SLOT_LOGO;

  if (imageName == "homerun") {
    slot = IPIXEL_SLOT_HOMERUN;
  } else if (imageName == "strike") {
    slot = IPIXEL_SLOT_BALL;
  } else if (imageName == "single") {
    slot = IPIXEL_SLOT_SINGLE;
  } else if (imageName == "out") {
    slot = IPIXEL_SLOT_FLYOUT;
  } else if (imageName == "contact") {
    slot = IPIXEL_SLOT_FOUL;
  }

  ipixelShowSlot(slot);
}

#endif
