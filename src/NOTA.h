#pragma once

#include <Arduino.h>
#include "./MD5.h"

#if defined(ESP8266) || defined(ESP32) || defined(ARDUINO_ARCH_ESP8266) || defined(ARDUINO_ARCH_ESP32)
#ifndef ESP
#define ESP
#endif // ESP
#elif defined(STM32) || defined(ARDUINO_ARCH_STM32)
#ifndef ARDUINO_ARCH_STM32
#define ARDUINO_ARCH_STM32
#endif // ARDUINO_ARCH_STM32
#include "./utility/internal_flash.h"
#else // UNKOWN PLATFORM
#error "Unknown platform"
#endif


// ESP8266
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
// ESP32
#elif defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
#include <WiFi.h>
#include "Update.h"
#include <WiFiUdp.h>
// ARDUINO_ARCH_STM32
#elif defined(ARDUINO_ARCH_STM32)
// Use the Ethernet library for STM32 with ArduinoOTA
#include <Ethernet.h>

#define UPDATE_ERROR_OK                 (0)
#define UPDATE_ERROR_WRITE              (1)
#define UPDATE_ERROR_ERASE              (2)
#define UPDATE_ERROR_READ               (3)
#define UPDATE_ERROR_SPACE              (4)
#define UPDATE_ERROR_SIZE               (5)
#define UPDATE_ERROR_STREAM             (6)
#define UPDATE_ERROR_MD5                (7)
#define UPDATE_ERROR_FLASH_CONFIG       (8)
#define UPDATE_ERROR_NEW_FLASH_CONFIG   (9)
#define UPDATE_ERROR_MAGIC_BYTE         (10)
#define UPDATE_ERROR_BOOTSTRAP          (11)
#define UPDATE_ERROR_SIGN               (12)
#define UPDATE_ERROR_NO_DATA            (13)
#define UPDATE_ERROR_OOM                (14)

#define U_FLASH   0
#define U_FS      100
#define U_SPIFFS  100
#define U_AUTH    200
#define U_TEST    201
#endif

#ifndef ENV
#define __XENV(x) #x
#define ENV(x) __XENV(x)
#endif /* ENV */

#define NOTA_VERSION "0.0.2"

static char ota_temp[128];

typedef enum {
    OTA_IDLE,
    OTA_WAITAUTH,
    OTA_RUNUPDATE
} ota_state_t;

typedef enum {
    OTA_AUTH_ERROR,
    OTA_BEGIN_ERROR,
    OTA_CONNECT_ERROR,
    OTA_RECEIVE_ERROR,
    OTA_END_ERROR
} ota_error_t;

class NOTAClass {
public:
    typedef std::function<void(void)> THandlerFunction;
    typedef std::function<void(ota_error_t)> THandlerFunction_Error;
    typedef std::function<void(unsigned int, unsigned int)> THandlerFunction_Progress;

    // MyFileStorageClass *storage = nullptr;

    NOTAClass();
    ~NOTAClass();

    //Sets the service port. Default 8266
    void setPort(uint16_t port);

    //Sets the storage object. Default internal flash
    // void setStorage(MyFileStorageClass &storage);

    //Sets the device hostname. Default esp8266-xxxxxx
    void setHostname(const char* hostname);
    String getHostname();

    //Sets the platform name. Default "Unknown"
    void setPlatform(const char* platform);
    String getPlatform();

    //Sets the password that will be required for OTA. Default NULL
    void setPassword(const char* password);

    //Sets the password as above but in the form MD5(password). Default NULL
    void setPasswordHash(const char* password);

    //Sets if the device should be rebooted after successful update. Default true
    void setRebootOnSuccess(bool reboot);

    //This callback will be called when OTA connection has begun
    void onRequest(THandlerFunction fn);

    //This callback will be called when OTA update has started
    void onStart(THandlerFunction fn);

    //This callback will be called when OTA has finished
    void onEnd(THandlerFunction fn);

    //This callback will be called when OTA encountered Error
    void onError(THandlerFunction_Error fn);

    //This callback will be called when OTA is receiving data
    void onProgress(THandlerFunction_Progress fn);

    //Starts the ArduinoOTA service
    void begin();

    //Call this in loop() to run the service. Also calls MDNS.update() when begin() or begin(true) is used.
    void handle();

    //Gets update command type after OTA has started. Either U_FLASH or U_FS
    int getCommand();

private:
    void listener();
    void ota_handle_idle();
    void ota_handle_auth();
    void ota_handle_update();
    int parseInt();
    String readStringUntil(char end);

    long _last_auth_time;
    long _last_update_time;
    int _port = 0;
    String _password;
    String _hostname;
    String _platform = "Unknown";
    String _nonce;
#ifdef ARDUINO_ARCH_STM32
    EthernetServer* _tcp_ota = nullptr;
    EthernetClient* ota_client = nullptr;
#else
    WiFiServer* _tcp_ota = nullptr;
    WiFiClient* ota_client = nullptr;
#endif
    bool _initialized = false;
    bool _rebootOnSuccess = true;
    ota_state_t _state = OTA_IDLE;
    int _size = 0;
    int _cmd = 0;
    uint16_t _ota_port = 0;
    uint16_t _ota_tcp_port = 0;
    IPAddress _ota_ip;
    String _program_hash_;

    THandlerFunction _request_callback = nullptr;
    THandlerFunction _start_callback = nullptr;
    THandlerFunction _end_callback = nullptr;
    THandlerFunction_Error _error_callback = nullptr;
    THandlerFunction_Progress _progress_callback = nullptr;
};

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_ARDUINOOTA)
extern NOTAClass OTA;
#endif



#ifndef LWIP_OPEN_SRC
#define LWIP_OPEN_SRC
#endif

#include "NOTA.h"


#define USE_GLOBAL_MDNS

#if defined(ESP8266)
#include <functional>
#include "MD5Builder.h"
#include "StreamString.h"

extern "C" {
#include "osapi.h"
#include "ets_sys.h"
#include "user_interface.h"
}

#include "lwip/opt.h"
#include "lwip/inet.h"
#include "lwip/igmp.h"
#include "lwip/mem.h"


#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_MDNS)
#include <ESP8266mDNS.h>
#endif

#elif defined(ESP32)
#include <functional>
#include <WiFiUdp.h>
#include "ESPmDNS.h"
#include "MD5Builder.h"
#include "Update.h"

#elif defined(ARDUINO_ARCH_STM32)
#include <functional>
#include <Ethernet.h>
//#include <ArduinoOTA.h>

#endif

#define OTA_DEBUG Serial

char reusable_hash[128];

String MD5(const char* text) {
    uint8_t* hash = MD5::make_hash((char*) text);
    //generate the digest (hex encoding) of our hash
    char* md5str = MD5::make_digest(hash, 16);
    strcpy(reusable_hash, md5str);
    free(hash);
    free(md5str);
    return String(reusable_hash);
}
String MD5(const String& text) { return MD5(text.c_str()); }
String MD5(long ms) { return MD5(String(ms)); }

NOTAClass::NOTAClass() {}

NOTAClass::~NOTAClass() {
    if (_tcp_ota) {
        delete _tcp_ota;
        _tcp_ota = 0;
    }
}

void NOTAClass::onRequest(THandlerFunction fn) { _request_callback = fn; }
void NOTAClass::onStart(THandlerFunction fn) { _start_callback = fn; }
void NOTAClass::onEnd(THandlerFunction fn) { _end_callback = fn; }
void NOTAClass::onProgress(THandlerFunction_Progress fn) { _progress_callback = fn; }
void NOTAClass::onError(THandlerFunction_Error fn) { _error_callback = fn; }
void NOTAClass::setPort(uint16_t port) { if (!_initialized && !_port && port) _port = port; }
// void NOTAClass::setStorage(MyFileStorageClass& storage) { if (!_initialized) this->storage = &storage; }
void NOTAClass::setHostname(const char* hostname) { if (!_initialized && !_hostname.length() && hostname) _hostname = hostname; }
String NOTAClass::getHostname() { return _hostname; }
void NOTAClass::setPlatform(const char* platform) { if (!_initialized && platform) _platform = platform; }
String NOTAClass::getPlatform() { return _platform; }
void NOTAClass::setPassword(const char* password) {
    if (!_initialized && !_password.length() && password) {
        _password = MD5(password);
    }
}

void NOTAClass::setPasswordHash(const char* password) { if (!_initialized && !_password.length() && password) _password = password; }
void NOTAClass::setRebootOnSuccess(bool reboot) { _rebootOnSuccess = reboot; }

void NOTAClass::begin() {
    if (_initialized) return;
    if (!_hostname.length()) {
#if defined(ESP8266)
        sprintf(ota_temp, "esp8266-%06x", ESP.getChipId());
#elif defined(ESP32) 
        uint8_t mac[6];
        WiFi.macAddress(mac);
        sprintf(ota_temp, "esp32-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
#endif
        _hostname = ota_temp;
    }
#if defined(ESP8266)
    if (!_port) _port = 8266;
#elif defined(ESP32)
    if (!_port) _port = 3232;
#elif defined(ARDUINO_ARCH_STM32)
    if (!_port) _port = 3232;
#endif
    if (_tcp_ota) {
        delete _tcp_ota;
        _tcp_ota = 0;
    }
#ifdef ARDUINO_ARCH_STM32
    _tcp_ota = new EthernetServer(_port);
    _tcp_ota->begin();
#else
    _tcp_ota = new WiFiServer(_port);
    _tcp_ota->begin(_port);
#endif
#if !defined(ARDUINO_ARCH_STM32) && defined(USE_GLOBAL_MDNS) && !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_MDNS)
    MDNS.begin(_hostname.c_str());
    MDNS.enableArduino(_port, _password.length() > 0);
#endif
    _initialized = true;
    _state = OTA_IDLE;
#ifdef ARDUINO_ARCH_STM32
    Serial.printf("OTA server at port %u\n", _port);
#else
    Serial.printf("OTA server at: %s.local:%u\n", _hostname.c_str(), _port);
#endif
}

int NOTAClass::parseInt() {
    if (!ota_client) return 0;
    int i = 0;
    uint8_t index = 0;
    char value;
    bool done = false;
    while (ota_client->available() && ota_client->peek() == ' ') ota_client->read();
    while (!done && ota_client->available() && ota_client->peek() && ota_client->peek() != ' ' && i <= 15) {
        i++;
        value = ota_client->read();
        if (value == '\n' || value == '\r') {
            ota_temp[index] = 0;
            done = true;
        } else ota_temp[index++] = value;
    }
    ota_temp[index] = 0;
    return atoi(ota_temp);
}

String NOTAClass::readStringUntil(char end) {
    if (!ota_client) return "";
    String res;
    int value;
    while (true) {
        value = ota_client->read();
        if (value < 0 || value == '\0' || value == end) return res;
        res += static_cast<char>(value);
    }
    return res;
}


void NOTAClass::ota_handle_idle() {
    delay(10);
    Serial.println("Incoming OTA update request ...");
    int cmd = this->parseInt();
    if (cmd != U_FLASH && cmd != U_SPIFFS) {
        Serial.printf("Unknown command: \"%d\"\n", cmd);
        while (ota_client->available()) ota_client->read();
        return;
    }
    _cmd = cmd;
    ota_client->read(); // skip ' '

    Serial.printf("OTA Update type: %s\n", cmd == U_FLASH ? "U_FLASH" : "U_FS");
#ifdef ESP
    ota_client->setNoDelay(true);
#endif
    Serial.printf("OTA program size: ");
    _size = this->parseInt();
    Serial.printf("%d\n", _size);
    ota_client->read(); // skip ' '
    Serial.printf("OTA program MD5 hash: ");
    _program_hash_ = readStringUntil('\n');
    _program_hash_.trim();
    while (ota_client->available()) ota_client->read();
    Serial.printf("%s\n", _program_hash_.c_str());
    bool error = false;
    if (_program_hash_.length() != 32) {
        Serial.println("Invalid MD5 hash length");
        _state = OTA_IDLE;
        sprintf(ota_temp, "ERR:HASH %s|/%s|/%s", NOTA_VERSION, _hostname.c_str(), _platform.c_str());
        ota_client->write((const char*) ota_temp, strlen(ota_temp));
        error = true;
    } else if (_password.length()) {
        _nonce = MD5(micros());
        sprintf(ota_temp, "AUTH %s %s|/%s|/%s", _nonce.c_str(), NOTA_VERSION, _hostname.c_str(), _platform.c_str());
        // Serial.printf("Requesting OTA authentication: %s\n", auth_req);
        ota_client->write((const char*) ota_temp, strlen(ota_temp));
        delay(100);
        _state = OTA_WAITAUTH;
        _last_auth_time = millis();
    } else {
        Serial.println("Authentication OK");
        sprintf(ota_temp, "OK %s|/%s|/%s", NOTA_VERSION, _hostname.c_str(), _platform.c_str());
        ota_client->write((const char*) ota_temp, strlen(ota_temp));
        delay(100);
        _state = OTA_RUNUPDATE;
        _last_update_time = millis();
    }
}

void NOTAClass::ota_handle_auth() {
    int cmd = this->parseInt();
    if (cmd != U_AUTH && cmd != U_TEST) {
        Serial.printf("Authentication failed: Wrong command \"%d\"\n", cmd);
        ota_client->write("ERR:CMD", 7);
        _state = OTA_IDLE;
        return;
    }
    if (cmd == U_AUTH && _size <= 0) {
        Serial.printf("Authentication failed: Invalid size %d\n", _size);
        ota_client->write("ERR:SIZE", 8);
        _state = OTA_IDLE;
        return;
    }
    Serial.printf("Authenticating ");
    ota_client->read();
    Serial.printf(".");
    String cnonce = readStringUntil(' ');
    Serial.printf(".");
    String response = readStringUntil('\n');
    while (ota_client->available()) ota_client->read();
    Serial.printf(".");
    if (cnonce.length() != 32 || response.length() != 32) {
        Serial.printf(" failed: Invalid key length\n");
        ota_client->write("ERR:KEY", 7);
        _state = OTA_IDLE;
        return;
    }
    Serial.printf(".");
    String challenge = _password + ':' + _nonce + ':' + cnonce;
    Serial.printf(".");
    String result = MD5(challenge);
    Serial.printf(".");
    if (result.equals(response)) {
        Serial.println(" OK");
        if (cmd == U_TEST) {
            _state = OTA_IDLE;
            delay(100);
            ota_client->write("OK", 2);
            delay(100);
            while (ota_client->available()) ota_client->read();
        } else { // Run update
            _state = OTA_RUNUPDATE;
            _last_update_time = millis();
        }
        return;
    } else {
        Serial.printf(" failed - wrong nonce - expected \"%s\" but got \"%s\"\n", result.c_str(), response.c_str());
        ota_client->write("ERR:AUTH", 9);
        delay(100);
        if (_error_callback) _error_callback(OTA_AUTH_ERROR);
        _state = OTA_IDLE;
    }
}




void NOTAClass::ota_handle_update() {
#ifdef ESP
    if (!Update.begin(_size, _cmd)) {
#elif defined(ARDUINO_ARCH_STM32) // Using ArduinoOTA with NO_OTA_NETWORK -> InternalStorage
    int ota_open_error = InternalStorage.open(_size);
    if (ota_open_error > 0) {
#endif
        Serial.println("Update Begin Error");
#if defined(ESP8266)
        StreamString ___e;
        Update.printError(___e);
        String ss = ___e.c_str();
#elif defined(ESP32)
        String ss = Update.errorString();
#else
        int32_t max_size = InternalStorage.maxSize();

        sprintf(ota_temp, "Unable to open InternalStorage with size %d, max size is %d", _size, max_size);
        switch (ota_open_error) {
            case 1: Serial.println("(1) Size overflow"); break;
            case 2: Serial.println("(2) HAL_FLASH_Unlock problem"); break;
            case 3: Serial.println("(3) HAL_FLASHEx_Erase problem"); break;
            case 4: Serial.println("(4) SectorError problem"); break;
            default: Serial.printf("(%d) Unknown error code\n", ota_open_error); break;
        }
        String ss = ota_temp;
#endif
        // Remove the trailing newline character from the error string
        bool done = false;
        for (int i = ss.length() - 1; i >= 0 && !done; i--) {
            if (ss[i] == '\n' || ss[i] == '\r') ss.remove(i);
            else done = true;
        }
        Serial.printf("Error: %s\n", ss.c_str());
        ota_client->printf("ERR: %s", ss.c_str());
        if (_error_callback) _error_callback(OTA_BEGIN_ERROR);
        delay(50);
        while (ota_client->available()) ota_client->read();
        _state = OTA_IDLE;
        return;
}
    while (ota_client->available()) ota_client->read();
    Serial.println("OTA Update started");

    if (_request_callback) _request_callback();
    if (_start_callback) _start_callback();
    // Serial.printf("Sketch start address: 0x%08X\n", FLASH_BASE + InternalStorage.SKETCH_START_ADDRESS);
    // Serial.printf("Page size: %d\n", InternalStorage.PAGE_SIZE);
    // Serial.printf("Max flash: %d \n", InternalStorage.MAX_FLASH);
    // Serial.printf("OTA sector: %d \n", InternalStorage.sector);
    // Serial.printf("OTA sector size: %d \n", InternalStorage.sector_size);
    // Serial.printf("OTA start address: 0x%08X \n", InternalStorage.storageStartAddress);
    // Serial.printf("OTA max sketch size: %d \n", InternalStorage.maxSketchSize);
    // Serial.printf("OTA page aligned length: %d \n", InternalStorage.pageAlignedLength);
    // Serial.printf("OTA EraseInitStruct.TypeErase: %d \n", InternalStorage.EraseInitStruct.TypeErase);
    // Serial.printf("OTA EraseInitStruct.NbSectors: %d \n", InternalStorage.EraseInitStruct.NbSectors);
    // Serial.printf("OTA EraseInitStruct.Banks: %d \n", InternalStorage.EraseInitStruct.Banks);
    // Serial.printf("OTA EraseInitStruct.Sector: %d \n", InternalStorage.EraseInitStruct.Sector);
    // Serial.printf("OTA EraseInitStruct.VoltageRange: %d \n", InternalStorage.EraseInitStruct.VoltageRange);


    ota_client->write("OK", 2);
#ifdef ESP
    Update.setMD5(_program_hash_.c_str());
#endif
    delayMicroseconds(10);
    if (_progress_callback) _progress_callback(0, _size);
    delay(500);
    // WiFiUDP::stopAll();
    // WiFiClient::stopAll();
#ifdef ESP
    ota_client->setNoDelay(true);
#endif
    uint32_t written = 0;
    uint32_t total = 0;
    int waited = 1000;
#ifdef ESP
    while (_state == OTA_RUNUPDATE && !Update.isFinished() && (ota_client->connected() || ota_client->available())) {
#else
    bool valid = true;
    while (valid && _state == OTA_RUNUPDATE && (ota_client->connected() || ota_client->available())) {
        // while (_state == OTA_RUNUPDATE && total < _size && valid && ota_client->connected() && ota_client->available()) {
#endif
        bool available = ota_client->available();
        if (!available && waited--) {
            delay(1);
            continue;
        }
        if (!available) {
            Serial.printf("\nReceive Failed: TIMEOUT\n");
            if (_error_callback) _error_callback(OTA_RECEIVE_ERROR);
            valid = false;
            break;
        }
        waited = 1000;
#ifdef ESP
        written = Update.write(*ota_client);
#else
        written = 0;
        while (valid && (ota_client->available())) {
            uint8_t b = ota_client->read();
            if (!InternalStorage.write(b)) {
                Serial.printf("\nReceive Failed: InternalStorage.write\n");
                if (_error_callback) _error_callback(OTA_RECEIVE_ERROR);
                valid = false;
                break;
            } else {
                if ((total + written) < 0xFF) {
                    Serial.printf("%02X ", b);
                }
            }
            written++;
#endif
            if (written >= InternalStorage.maxSize()) {
                Serial.printf("\nReceive Failed: SIZE OVERFLOW\n");
                if (_error_callback) _error_callback(OTA_RECEIVE_ERROR);
                valid = false;
                break;
            }
            if (written > _size) {
                Serial.printf("\nReceive Failed: SIZE MISMATCH\n");
                if (_error_callback) _error_callback(OTA_RECEIVE_ERROR);
                valid = false;
                break;
            }
        }
        if (written > 0) {
            ota_client->print(written, DEC);
            total += written;
            if (_progress_callback) _progress_callback(total, _size);
            if (total >= _size) break;
        }
    }
#ifdef ESP
    if (Update.end()) {
#else
    // TODO: verify MD5 hash
    if (valid && _state == OTA_RUNUPDATE && total == _size) {
#endif
        Serial.printf("Update Success: %u\n", total);

        if (_end_callback) _end_callback();

        delay(10);
        InternalStorage.close();


        // Ensure last count packet has been sent out and not combined with the final OK
        ota_client->flush();
        delay(2000);
        ota_client->print("OK");
        ota_client->flush();
        delay(1000);
        ota_client->stop();
        delay(100);
        Serial.printf("Update Success\n");
#ifdef ARDUINO_ARCH_STM32
        InternalStorage.apply();
#endif
        if (_rebootOnSuccess) {
            Serial.printf("Rebooting after successful update\n");
            //let serial/network finish tasks that might be given in _end_callback
            delay(1000);
#ifdef ESP
            ESP.restart();
#else
            NVIC_SystemReset();
#endif
        } else {
            Serial.printf("Skipping reboot after successful update\n");
        }
    } else {
        if (_error_callback) _error_callback(OTA_END_ERROR);
#ifdef ESP
        Update.printError(*ota_client);
        Update.printError(Serial);
#endif
        delay(100);
    }
    _state = OTA_IDLE;
    while (ota_client->available()) ota_client->read();
    }


void NOTAClass::listener() {
    // Check if server is started
    if (!_tcp_ota) {
        Serial.println("OTA Server not running ...");
        this->begin();
        return;
    }
    if (_state == OTA_WAITAUTH && (_last_auth_time + 5000UL) < millis()) {
        _state = OTA_IDLE;
        Serial.println("OTA Authentication timeout");
    }
    if (_state == OTA_RUNUPDATE && (_last_update_time + 5000UL) < millis()) {
        _state = OTA_IDLE;
        Serial.println("OTA Update timeout");
    }
    auto client = _tcp_ota->available();
    if (!client.available()) return;
    delay(10);
    // Check if data is available
    ota_client = &client;
#ifdef ESP
    Serial.printf("Client with IP %s connected\n", client.remoteIP().toString().c_str());
#else 
    IPAddress ip = client.remoteIP();
    Serial.printf("Client with IP %d.%d.%d.%d connected\n", ip[0], ip[1], ip[2], ip[3]);
#endif
    if (_state == OTA_IDLE) ota_handle_idle();
    if (_state == OTA_WAITAUTH) ota_handle_auth();
    if (_state == OTA_RUNUPDATE) ota_handle_update();
    while (ota_client->available()) ota_client->read();
}

//this needs to be called in the loop()
void NOTAClass::handle() {
    if (!_initialized) return;
    this->listener();

#if defined(ESP8266) && defined(USE_GLOBAL_MDNS) && !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_MDNS)
    MDNS.update(); //handle MDNS update as well, given that OTA_TCP relies on it anyways
#endif
}

int NOTAClass::getCommand() { return _cmd; }

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_ARDUINOOTA)
NOTAClass OTA;
#endif
