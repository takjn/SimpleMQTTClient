// ESP8266 AT firmware MQTT client sample
// http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/mqtt-v3.1.1.html

#include <Arduino.h>

#define PIN_LIGHT 17
#define PIN_BUTTON 10

#ifdef GRROSE
#define Serial2 Serial6
#endif

#ifdef GRCITRUS
#define Serial2 Serial3
#define LED_BUILTIN PIN_LED0
#endif

#define WIFI_SSID "enter_your_wifi_ssid"
#define WIFI_PASS "enter_your_wifi_pass"

// MODE – Make IoT a reality for your business
// https://www.tinkermode.com/
#define MQTT_SERVER "mqtt.tinkermode.com"
#define MQTT_PORT 1883  // TCP=1883, SSL=8883
#define MQTT_KEEP_ALIVE 60
#define USER_NAME "enter_your_device_id"
#define PASSWORD "enter_your_device_API_key"
#define PUB_TOPIC "/devices/" + USER_NAME + "/event"
#define SUB_TOPIC "/devices/" + USER_NAME + "/command"

unsigned long last_millis;

char at_command[128];
char buffer[1024];

bool waitResponse(const char *expected_string, unsigned int timeout) {
    int length = strlen(expected_string);
    unsigned long time_out = millis() + timeout;

    char c = 0;
    int idx = 0;

    while (time_out > millis()) {
        if (Serial2.available()) {
            c = Serial2.read();
            if (c == expected_string[idx]) {
                idx++;

                if (idx == length) {
                    return true;
                }
            } else {
                idx = 0;
            }
        }
    }

    return false;
}

void halt(const char *buff) {
    int stat = 0;
    Serial.println(buff);
    while (1) {
        if (LED_BUILTIN) {
            digitalWrite(LED_BUILTIN, stat);
            stat = 1 - stat;
            delay(100);
        }
    }
}

int buildConnectPacket(char *buf, int keep_alive, const char *client_id, const char *will_topic, const char *will_msg, const char *user_name, const char *password) {
    // calculate Remaining Length
    int client_id_len = strlen(client_id);
    int will_topic_len = strlen(will_topic);
    int will_msg_len = strlen(will_msg);
    int user_name_len = strlen(user_name);
    int password_len = strlen(password);

    int remaining_length = 10;
    remaining_length += (2 + client_id_len);
    if (will_topic_len > 0 && will_msg_len > 0) {
        remaining_length += (2 + will_topic_len + 2 + will_msg_len);
    }
    if (user_name_len > 0 && password_len > 0) {
        remaining_length += (2 + user_name_len + 2 + password_len);
    }

    // 3.1 CONNECT – Client requests a connection to a Server
    // 3.1.1  Fixed header
    int idx = 0;

    // MQTT Control Packet type
    buf[idx++] = 0x10;

    // Remaining Length -  see section 2.2.3.
    int len = remaining_length;
    int encodedByte = 0;
    do {
        encodedByte = len % 128;
        len = len / 128;
        // if there are more data to encode, set the top bit of this byte
        if (len > 0) {
            encodedByte = encodedByte | 128;
        }

        buf[idx++] = encodedByte;
    } while (len > 0);

    // 3.1.2 Variable header
    buf[idx++] = 0x00;
    buf[idx++] = 0x04;
    buf[idx++] = 'M';
    buf[idx++] = 'Q';
    buf[idx++] = 'T';
    buf[idx++] = 'T';
    buf[idx++] = 0x04;
    buf[idx] = 0x02;
    if (will_topic_len > 0 && will_msg_len > 0) {
        buf[idx] = 0x04 | buf[idx];
    }
    if (user_name_len > 0 && password_len > 0) {
        buf[idx] = 0xC0 | buf[idx];
    }
    idx++;
    buf[idx++] = 0x00;
    buf[idx++] = keep_alive;

    // 3.1.3 Payload
    // 3.1.3.1 Client Identifier
    buf[idx++] = client_id_len >> 8;
    buf[idx++] = client_id_len & 0xFF;
    for (int i = 0; i < client_id_len; i++) {
        buf[idx++] = client_id[i];
    }

    // 3.1.3.2 Will Topic
    // 3.1.3.3 Will Message
    if (will_topic_len > 0 && will_msg_len > 0) {
        buf[idx++] = will_topic_len >> 8;
        buf[idx++] = will_topic_len & 0xFF;
        for (int i = 0; i < will_topic_len; i++) {
            buf[idx++] = will_topic[i];
        }
        buf[idx++] = will_msg_len >> 8;
        buf[idx++] = will_msg_len & 0xFF;
        for (int i = 0; i < will_msg_len; i++) {
            buf[idx++] = will_msg[i];
        }
    }

    // 3.1.3.4 User Name
    // 3.1.3.5 Password
    if (user_name_len > 0 && password_len > 0) {
        buf[idx++] = user_name_len >> 8;
        buf[idx++] = user_name_len & 0xFF;
        for (int i = 0; i < user_name_len; i++) {
            buf[idx++] = user_name[i];
        }
        buf[idx++] = password_len >> 8;
        buf[idx++] = password_len & 0xFF;
        for (int i = 0; i < password_len; i++) {
            buf[idx++] = password[i];
        }
    }

    return idx;
}

int buildPublishPacket(char *buf, const char *topic, const char *message) {
    //  3.3 PUBLISH – Publish message
    //  3.3.1 Fixed header
    int idx = 0;
    int topic_length = strlen(topic);
    int message_length = strlen(message);
    buf[idx++] = 0x30; // DUP=0, QoS=0 (At most once delivery), RETAIN=0
    buf[idx++] = 2 + topic_length + message_length;

    //  3.3.2  Variable header
    //  3.3.2.1 Topic Name
    buf[idx++] = topic_length >> 8;
    buf[idx++] = topic_length & 0xFF;
    for (int i = 0; i < topic_length; i++) {
        buf[idx++] = topic[i];
    }

    //  3.3.3 Payload
    for (int i = 0; i < message_length; i++) {
        buf[idx++] = message[i];
    }

    return idx;
}

int buildSubscribePacket(char *buf, const char *topic) {
    // 3.8 SUBSCRIBE - Subscribe to topics
    // 3.8.1 Fixed header
    int idx = 0;
    int topic_length = strlen(topic);
    buf[idx++] = 0x82;
    buf[idx++] = 4 + topic_length + 1;

    // 3.8.2 Variable header
    //  Packet Identifier set to 0x1
    buf[idx++] = 0x00;
    buf[idx++] = 0x01;

    // 3.8.3 Payload
    // Length
    buf[idx++] = topic_length >> 8;
    buf[idx++] = topic_length & 0xFF;

    // Topic Name
    for (int i = 0; i < topic_length; i++) {
        buf[idx++] = topic[i];
    }

    // Requested QoS = 0
    buf[idx++] = 0x00;

    return idx;
}

bool sendPacket(const char *packet, int length, const char *expected_string) {
    sprintf(at_command, "AT+CIPSEND=%02d\r\n", length);
    Serial2.print(at_command);
    if (!waitResponse("OK\r\n>", 5000)) {
        halt("AT+CIPSEND failed");
    }
    for (int i = 0; i < length; i++) {
        Serial2.write(packet[i]);
    }
    return waitResponse(expected_string, 10000);
}

int getPacket(char *packet) {
    char c = 0;
    int length = 0;
    int idx = 0;

    if (Serial2.available()) {
        // ESP8266 received data structure
        // "+IPD," + length + ":" + data

        // Step.1: Wait "+IPD,"
        if (waitResponse("+IPD,", 1000)) {
            // step 2: Get length
            while (true) {
                delay(1); // To improve stability
                c = Serial2.read();
                if (c >= '0' && c <= '9') {
                    buffer[idx++] = c;
                } else if (c == ':') {
                    buffer[idx] = '\0';
                    break;
                } else {
                    // unexpected error
                    idx = 0;
                    break;
                }
            }
            if (idx > 0) {
                length = atoi(buffer);
                // Serial.print("Received length: ");
                // Serial.println(length);
            }

            // Step 3: Get data
            idx = 0;
            for (int i = 0; i < length; i++) {
                delay(1); // To improve stability
                c = Serial2.read();
                if (c < 0) {
                    // unexpected error
                    idx = 0;
                    break;
                }
                if (i < 3) {
                    // skip MQTT header
                } else {
                    // get payload
                    buffer[idx++] = c;
                }
                // delay(5); // just in case
            }
            buffer[idx] = '\0';
            // Serial.print("Received data: ");
            // Serial.println(buffer);
        }
    }
    return idx;
}

void connectWiFi() {
    Serial2.print("AT+CWMODE=1\r\n");
    if (!waitResponse("OK", 1000)) {
        halt("AT+CWMODE failed");
    }

    sprintf(at_command, "AT+CWJAP=\"%s\",\"%s\"\r\n", WIFI_SSID, WIFI_PASS);
    Serial2.print(at_command);
    if (!waitResponse("OK", 10000)) {
        halt("AT+CWJAP failed");
    }
}

void connectServer() {
    int packet_length;

    if (MQTT_PORT == 1883) {
        sprintf(at_command, "AT+CIPSTART=\"TCP\",\"%s\",%d\r\n", MQTT_SERVER, MQTT_PORT);
        Serial2.print(at_command);
        if (!waitResponse("OK", 5000)) {
            halt("AT+CIPSTART failed");
        }
    } else {
        Serial.println("SSL Start");
        Serial2.print("AT+CIPSSLSIZE=4096\r\n");
        if (!waitResponse("OK", 1000)) {
            halt("AT+CIPSSLSIZE failed");
        }
        sprintf(at_command, "AT+CIPSTART=\"SSL\",\"%s\",%d\r\n", MQTT_SERVER, MQTT_PORT);
        Serial2.print(at_command);
        if (!waitResponse("OK", 5000)) {
            halt("AT+CIPSTART failed");
        }
    }

    Serial.print("MQTT CONNECT - ");
    Serial.println(MQTT_SERVER);
    packet_length = buildConnectPacket(buffer, MQTT_KEEP_ALIVE, "", "", "", USER_NAME, PASSWORD);
    if (!sendPacket(buffer, packet_length, "+IPD")) { // TODO: check the return code
        halt("CONNACK failed");
    }

    Serial.print("MQTT SUBSCRIBE - ");
    Serial.println(SUB_TOPIC);
    packet_length = buildSubscribePacket(buffer, SUB_TOPIC);
    if (!sendPacket(buffer, packet_length, "+IPD")) { // TODO: check the return code
        halt("SUBACK failed");
    }
}

void setup() {
    Serial.begin(115200);
    Serial2.begin(115200);

    pinMode(PIN_BUTTON, INPUT_PULLUP);
    pinMode(PIN_LIGHT, OUTPUT);

    // Connect to WiFi
    connectWiFi();

    // Connect to MQTT Server
    connectServer();

    last_millis = millis();
}

void loop() {
    // Send a PING packet to the server to avoid disconnect
    static int timer = 0;
    static int led_state = 1;
    unsigned long now = millis();

    if ((now - last_millis) < 0) {
        last_millis = now;
    }
    if ((now - last_millis) > 1000) {
        last_millis = now;
        timer++;
        digitalWrite(LED_BUILTIN, led_state);
        led_state = 1 - led_state;

        if (timer == MQTT_KEEP_ALIVE) { //  PingReq
            Serial.println("MQTT PING");
            byte pingreq_packet[] = {0xc0, 0x00};
            sendPacket((char *)pingreq_packet, 2, "+IPD");
            timer = 0;
        }
    }

    // Send a PUBLISH packet to the server
    if (digitalRead(PIN_BUTTON) == 0) {
        Serial.print("MQTT PUBLISH - ");
        char payload[128];
        sprintf(payload, "{ \"eventType\": \"test\", \"eventData\": { \"value\": %d } }", now);
        Serial.println(payload);
        int packet_length = buildPublishPacket(buffer, PUB_TOPIC, payload);
        if (!sendPacket(buffer, packet_length, "SEND OK")) {
            halt("PUBLISH failed");
        }
    }

    // Get a PUBLISH packet from the server
    if (getPacket(buffer) > 0) {
        String result = String((char *)buffer);
        if (result.indexOf(SUB_TOPIC) > 0) {
            Serial.println("MQTT PUBACK");
            byte puback_packet[] = {0x40, 0x02};
            if (!sendPacket((char *)puback_packet, 2, "SEND OK")) {
                halt("PUBACK failed");
            }

            if (result.indexOf("switch\":0") > 0) {
                Serial.println("switch: off");
                digitalWrite(PIN_LIGHT, LOW);
            } else if (result.indexOf("switch\":1") > 0) {
                Serial.println("switch: on");
                digitalWrite(PIN_LIGHT, HIGH);
            }
        }
    }
}

