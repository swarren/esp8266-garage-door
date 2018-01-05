/*
 * Copyright (c) 2017, Stephen Warren
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
 
#include <math.h>
#include <FS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266LLMNR.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include "ESPAsyncTCP.h"

// Development on Adafruit Huzzah
#if 0
#define DEVID "-dev"
#define PIN_BTN 0
#if 1
#define DOOR0_NAME "Dev Door"
#define PIN_DOOR_0 5
#define PIN_ULTRA_TRIG_0 16
#define PIN_ULTRA_ECHO_0 4
#endif
#if 1
#define DOOR1_NAME "Second Door"
#define PIN_DOOR_1 14
#define PIN_ULTRA_TRIG_1 12
#define PIN_ULTRA_ECHO_1 13
#endif
#endif

// Production for main garage on NodeMCU; rev 1 PCB
#if 0
#define DEVID "-main"
#define PIN_BTN 0
#define DOOR0_NAME "Stephen's Door"
#define PIN_DOOR_0 16
#define PIN_ULTRA_TRIG_0 5
#define PIN_ULTRA_ECHO_0 4
#define DOOR1_NAME "Tina's Door"
#define PIN_DOOR_1 14
#define PIN_ULTRA_TRIG_1 12
#define PIN_ULTRA_ECHO_1 13
#endif

// Production for main garage on NodeMCU; rev 2 PCB
#if 0
#define DEVID "-main"
#define PIN_BTN 0
#define DOOR0_NAME "Stephen's Door"
#define PIN_DOOR_0 4
#define PIN_ULTRA_TRIG_0 16
#define PIN_ULTRA_ECHO_0 14
#define DOOR1_NAME "Tina's Door"
#define PIN_DOOR_1 5
#define PIN_ULTRA_TRIG_1 12
#define PIN_ULTRA_ECHO_1 13
#endif

// Production for third garage on NodeMCU; rev 1 PCB (2nd door pins)
#if 1
#define DEVID "-third"
#define PIN_BTN 0
#define DOOR0_NAME "Third Garage"
#define PIN_DOOR_0 14
#define PIN_ULTRA_TRIG_0 12
#define PIN_ULTRA_ECHO_0 13
#endif

#define CLOSED_MIN_ECHO_TIME \
  (1000000 /* s->us */ * 2 /* there and back */ * 0.3 /* meters */ / \
    343 /* sound_speed_meters_per_sec */)

#if defined(PIN_ULTRA_TRIG_0) || defined(PIN_ULTRA_TRIG_1)
#define DEBUG_ULTRA 0

class UltraEcho {
public:
  UltraEcho(int pin) :
    m_pin(pin),
    m_wait_for_echo_start(true),
    m_prev_closed(true) {
  }

  void pre_trigger(void) {
    m_wait_for_echo_start = true;
  }

  bool is_closed(void) {
    return __builtin_popcount(m_closed_hist & 0xff) >= 6;
  }

  String history_string(void) {
    int hist_bits = sizeof(m_closed_hist) * 8;
    char s[hist_bits + 1];
    for (int i = 0; i < hist_bits; i++)
      s[i] = (m_closed_hist & (1ULL << (hist_bits - i - 1))) ? '*' : '-';
    s[hist_bits] = 0;
    return s;
  }

  void isr_echo(void (*closed_change_callback)(bool closed)) {
    unsigned long t = micros();
    int val = digitalRead(m_pin);
#if DEBUG_ULTRA
    Serial.printf("Echo pin %d val %d waiting %d at %lu\n", m_pin, val, (int)m_wait_for_echo_start, t);
#endif
    if (m_wait_for_echo_start && val) {
#if DEBUG_ULTRA
      Serial.printf("Echo starts...\n");
#endif
      m_ts_echo_start = t;
      m_wait_for_echo_start = false;
    } else if (!m_wait_for_echo_start && !val) {
#if DEBUG_ULTRA
      Serial.printf("Echo done...\n");
#endif
      m_echo_time = t - m_ts_echo_start;
#if DEBUG_ULTRA
      Serial.printf("Echo pin %d time %d us\n", m_pin, m_echo_time);
#endif
      bool closed = (m_echo_time < CLOSED_MIN_ECHO_TIME);
#if DEBUG_ULTRA
      Serial.printf("Closed? %d\n", (int)closed);
#endif
      m_closed_hist <<= 1;
      m_closed_hist |= !!closed;
      bool new_closed = is_closed();
      if (new_closed != m_prev_closed) {
#if DEBUG_ULTRA
        Serial.printf("Closed changed\n");
#endif
        if (closed_change_callback)
          closed_change_callback(new_closed);
        m_prev_closed = new_closed;
      }
#if DEBUG_ULTRA
      Serial.printf("Closed hist: 0x%x ?%d\n", m_closed_hist, !!is_closed());
#endif
    }
  }

private:
  int m_pin;
  bool m_wait_for_echo_start;
  bool m_prev_closed;
  unsigned long m_ts_echo_start;
  unsigned long m_echo_time;
  unsigned int m_closed_hist;
};
#endif

enum RelayControllerState {
  RC_STATE_IDLE,
  RC_STATE_HIGH_TIMER,
  RC_STATE_LOW_TIMER,
};

class RelayController {
public:
  RelayController(int pin) :
    m_pin(pin),
    m_trig_count(0),
    m_state(RC_STATE_IDLE),
    m_state_start(0) {
  }

  void iter() {
    switch (m_state) {
    case RC_STATE_HIGH_TIMER:
      if ((millis() - m_state_start) < 500)
        break;
      m_state = RC_STATE_LOW_TIMER;
      m_state_start = millis();
      digitalWrite(m_pin, LOW);
      break;
    case RC_STATE_LOW_TIMER:
      if ((millis() - m_state_start) < 500)
        break;
      m_state = RC_STATE_IDLE;
      // deliberate fall-through
    case RC_STATE_IDLE:
    default:
      if (!m_trig_count)
        break;
      m_trig_count--;
      m_state = RC_STATE_HIGH_TIMER;
      m_state_start = millis();
      digitalWrite(m_pin, HIGH);
      break;
    }
  }

  void trigger() {
    m_trig_count++;
  }

private:
  int m_pin;
  int m_trig_count;
  enum RelayControllerState m_state;
  unsigned long m_state_start;
};

#define DEBUG_SMTP 0

enum SMTPClientState {
  SMTP_STATE_IDLE,
  SMTP_STATE_WAIT_CONNECT,
  SMTP_STATE_WAIT_SIGNON,
  SMTP_STATE_WAIT_FEATURES,
  SMTP_STATE_WAIT_MAIL_FROM_OK,
  SMTP_STATE_WAIT_RCPT_TO_OK,
  SMTP_STATE_WAIT_DATA_RESPONSE,
  SMTP_STATE_WAIT_END_DATA_RESPONSE,
  SMTP_STATE_WAIT_QUIT_RESPONSE,
  SMTP_STATE_WAIT_CLOSED,
};

class LineBuf {
public:
  LineBuf(void) : m_filled(0) {
  }

  void clear(void) {
    m_filled = 0;
  }

  size_t append(void *data, size_t len) {
    size_t space = sizeof(m_data) - m_filled - 1;
    size_t consumed = (space < len) ? space : len;
    memcpy(&m_data[m_filled], data, consumed);
    m_filled += consumed;
    m_data[m_filled] = '\0';
    return consumed;
  }

  char *get_line(size_t *token) {
    char *eol = strstr(m_data, "\r\n");
    if (!eol) {
      *token = 0;
      return NULL;
    }
    *eol = '\0';
    *token = eol - m_data + 2;
#if DEBUG_SMTP
    Serial.printf("SMTP << %s\n", m_data);
#endif
    return m_data;
  }

  void consume_line(size_t token) {
    memcpy(m_data, &m_data[token], m_filled - token);
    m_filled -= token;
  }

private:
  char m_data[128];
  size_t m_filled;
};

class SMTPClient {
public:
  SMTPClient() :
    m_state(SMTP_STATE_IDLE),
    m_data_door_name(NULL),
    m_data_closed(false),
    m_client(),
    m_ibuf()
  {
    m_client.onTimeout(
      [](void *obj, AsyncClient* c, uint32_t time) {
        ((SMTPClient *)(obj))->on_timeout();
      }, this);
    m_client.onConnect(
      [](void *obj, AsyncClient* c) {
        ((SMTPClient *)(obj))->on_connect();
      }, this);
    m_client.onDisconnect(
      [](void *obj, AsyncClient* c) {
        ((SMTPClient *)(obj))->on_disconnect();
      }, this);
    m_client.onData(
      [](void *obj, AsyncClient* c, void *data, size_t len) {
        ((SMTPClient *)(obj))->on_data(data, len);
      }, this);
  }

  bool start_email(const char *door_name, bool closed) {
#if DEBUG_SMTP
    Serial.printf("SMTP (%d) start_email()\n", m_state);
#endif
    if (m_state != SMTP_STATE_IDLE) {
#if DEBUG_SMTP
      Serial.printf("SMTP skip start due to active\n");
#endif
      return false;
    }
    m_data_door_name = door_name;
    m_data_closed = closed;
    m_ibuf.clear();
    bool in_progress = m_client.connect("192.168.63.2", 25);
    if (!in_progress) {
#if DEBUG_SMTP
      Serial.printf("SMTP skip start due to connect() failed\n");
#endif
      return false;
    }
    set_state(SMTP_STATE_WAIT_CONNECT);
    return true;
  }

  void iter(void) {
    if (m_state == SMTP_STATE_IDLE)
      return;

    unsigned long t = millis();
    if ((t - m_state_time) < 1000)
      return;

#if DEBUG_SMTP
    Serial.printf("SMTP (%d) iter timeout\n", m_state);
#endif
    close_connection();
  }

private:
  SMTPClientState m_state;
  unsigned long m_state_time;
  const char *m_data_door_name;
  bool m_data_closed;
  AsyncClient m_client;
  LineBuf m_ibuf;

  void set_state(SMTPClientState new_state) {
#if DEBUG_SMTP
    if (new_state != m_state)
      Serial.printf("SMTP state -> %d\n", new_state);
#endif
    m_state = new_state;
    m_state_time = millis();
  }

  void close_connection() {
    switch (m_state) {
    case SMTP_STATE_IDLE:
    case SMTP_STATE_WAIT_CLOSED:
      return;
    default:
      break;
    }
    m_client.close();
    set_state(SMTP_STATE_WAIT_CLOSED);
  }

  void on_timeout() {
#if DEBUG_SMTP
    Serial.printf("SMTP (%d) timeout\n", m_state);
#endif
    close_connection();
  }

  void on_connect() {
#if DEBUG_SMTP
    Serial.printf("SMTP (%d) connected\n", m_state);
#endif
    switch (m_state) {
    case SMTP_STATE_WAIT_CONNECT:
      set_state(SMTP_STATE_WAIT_SIGNON);
      break;
    default:
      close_connection();
      break;
    }
  }

  void on_disconnect() {
#if DEBUG_SMTP
    Serial.printf("SMTP (%d) disconnected\n", m_state);
#endif
    set_state(SMTP_STATE_IDLE);
  }

  void on_data(void *data, size_t len) {
#if DEBUG_SMTP
    Serial.printf("SMTP (%d) data RX (%d)\n", m_state, len);
#endif
    while (len) {
      size_t consumed = m_ibuf.append(data, len);
      if (!consumed) {
        close_connection();
        return;
      }
      data += consumed;
      len -= consumed;
      switch (m_state) {
      case SMTP_STATE_WAIT_SIGNON:
        on_data_wait_signon();
        break;
      case SMTP_STATE_WAIT_FEATURES:
        on_data_wait_features();
        break;
      case SMTP_STATE_WAIT_MAIL_FROM_OK:
        on_data_wait_mail_from_ok();
        break;
      case SMTP_STATE_WAIT_RCPT_TO_OK:
        on_data_wait_rcpt_to_ok();
        break;
      case SMTP_STATE_WAIT_DATA_RESPONSE:
        on_data_wait_data_reponse();
        break;
      case SMTP_STATE_WAIT_END_DATA_RESPONSE:
        on_data_wait_end_data_response();
        break;
      case SMTP_STATE_WAIT_QUIT_RESPONSE:
        on_data_wait_quit_response();
        break;
      default:
        close_connection();
        break;
      }
    }
  }

  void send(const char *line) {
#if DEBUG_SMTP
    Serial.printf("SMTP >> %s", line);
#endif
    m_client.write(line);
  }

  void on_data_wait_signon() {
    size_t token;
    char *line = m_ibuf.get_line(&token);
    if (!line)
      return;
    bool ok = !strncmp(line, "220 ", 4);
    m_ibuf.consume_line(token);
    if (!ok) {
      close_connection();
      return;
    }
    send("EHLO garage-door" DEVID ".local\r\n");
    set_state(SMTP_STATE_WAIT_FEATURES);
  }

  void on_data_wait_features() {
    do {
      size_t token;
      char *line = m_ibuf.get_line(&token);
      if (!line)
        return;
      bool cont = !strncmp(line, "250-", 4);
      bool last = !strncmp(line, "250 ", 4);
      m_ibuf.consume_line(token);
      if (!cont && !last) {
        close_connection();
        return;
      }
      if (last)
        break;
    } while (true);
    send("MAIL FROM: <garage-door" DEVID "@wwwdotorg.org>\r\n");
    set_state(SMTP_STATE_WAIT_MAIL_FROM_OK);
  }

  void on_data_wait_mail_from_ok() {
    size_t token;
    char *line = m_ibuf.get_line(&token);
    if (!line)
      return;
    bool ok = !strncmp(line, "250 ", 4);
    m_ibuf.consume_line(token);
    if (!ok) {
      close_connection();
      return;
    }
    send("RCPT TO: <s-garage-door" DEVID "@wwwdotorg.org>\r\n");
    set_state(SMTP_STATE_WAIT_RCPT_TO_OK);
  }

  void on_data_wait_rcpt_to_ok() {
    size_t token;
    char *line = m_ibuf.get_line(&token);
    if (!line)
      return;
    bool ok = !strncmp(line, "250 ", 4);
    m_ibuf.consume_line(token);
    if (!ok) {
      close_connection();
      return;
    }
    send("DATA\r\n");
    set_state(SMTP_STATE_WAIT_DATA_RESPONSE);
  }

  void on_data_wait_data_reponse() {
    size_t token;
    char *line = m_ibuf.get_line(&token);
    if (!line)
      return;
    bool ok = !strncmp(line, "354 ", 4);
    m_ibuf.consume_line(token);
    if (!ok) {
      close_connection();
      return;
    }
    char msg[128];
    sprintf(msg,
      "From: <garage-door" DEVID "@wwwdotorg.org>\r\n"
      "To: <s-garage-door" DEVID "@wwwdotorg.org>\r\n"
      "Subject: %s now %s.\r\n"
      ".\r\n",
      m_data_door_name, m_data_closed ? "CLOSED" : "OPEN");
    send(msg);
    set_state(SMTP_STATE_WAIT_END_DATA_RESPONSE);
  }

  void on_data_wait_end_data_response() {
    size_t token;
    char *line = m_ibuf.get_line(&token);
    if (!line)
      return;
    bool ok = !strncmp(line, "250 ", 4);
    m_ibuf.consume_line(token);
    if (!ok) {
      close_connection();
      return;
    }
    send("QUIT\r\n");
    set_state(SMTP_STATE_WAIT_QUIT_RESPONSE);
  }

  void on_data_wait_quit_response() {
    size_t token;
    char *line = m_ibuf.get_line(&token);
    if (!line)
      return;
    bool ok = !strncmp(line, "250 ", 4);
    m_ibuf.consume_line(token);
    if (!ok) {
      close_connection();
      return;
    }
    close_connection();
  }
};
SMTPClient smtp_client;

struct SMTPQueueEntry {
  const char *door_name;
  bool closed;
};
template <int QLEN> class SMTPQueue {
public:
  SMTPQueue() :
    m_num_entries(0)
  {
  }

  void add(const char *door_name, bool closed) {
    noInterrupts();
    if (m_num_entries == QLEN)
      goto out;
    m_entries[m_num_entries].door_name = door_name;
    m_entries[m_num_entries].closed = closed;
    m_num_entries++;
out:
    interrupts();
  }

  void iter() {
    if (!m_num_entries)
      return;
    bool started = smtp_client.start_email(m_entries[0].door_name,
      m_entries[0].closed);
    if (!started)
      return;
    noInterrupts();
    m_num_entries--;
    memcpy(m_entries, &m_entries[1], m_num_entries * sizeof(m_entries[0]));
    interrupts();
  }

private:
  struct SMTPQueueEntry m_entries[QLEN];
  volatile int m_num_entries;
};
SMTPQueue<4> smtp_queue;

static String ssid;
static String wifipw;
RelayController relay0(PIN_DOOR_0);
#ifdef PIN_DOOR_1
RelayController relay1(PIN_DOOR_1);
#endif
#ifdef PIN_ULTRA_TRIG_0
static UltraEcho ultra_echo_0(PIN_ULTRA_ECHO_0);
#endif
#ifdef PIN_ULTRA_TRIG_1
static UltraEcho ultra_echo_1(PIN_ULTRA_ECHO_1);
#endif
static ESP8266WebServer web_server(80);
static ESP8266HTTPUpdateServer http_updater;

#ifdef PIN_ULTRA_TRIG_0
void isr_ultra_echo_0() {
  ultra_echo_0.isr_echo([](bool new_closed) {
    smtp_queue.add(DOOR0_NAME, new_closed);
  });
}
#endif

#ifdef PIN_ULTRA_TRIG_1
void isr_ultra_echo_1() {
  ultra_echo_1.isr_echo([](bool new_closed) {
    smtp_queue.add(DOOR1_NAME, new_closed);
  });
}
#endif

bool web_client_is_sta() {
  return web_server.client().localIP() == WiFi.localIP();
}

bool web_client_is_ap() {
  return web_server.client().localIP() == WiFi.softAPIP();
}

bool str_isprint(String s) {
  for (int i = 0; i < s.length(); i++)
    if (!isprint(s[i]))
      return false;
  return true;
}

void handle_http_not_found() {
  web_server.send(404, "text/plain", "Not Found");
}

const char html_unconfigured_ap_root[] = { "\
<html\
<head>\
<title>Set Configuration</title>\
</head>\
<body>\
<p>Please set configuration:</p>\
<form method=\"POST\" action=\"/set_config\">\
WiFi Network (SSID): <input type=\"text\" name=\"SSID\"/><br/>\
WiFi Password: <input type=\"text\" name=\"WIFIPW\"/><br/>\
<input type=\"submit\" value=\"Configure\"/>\
</form>\
</body>\
</html>\
"
};

void handle_http_unconfigured_ap_get_root() {
  web_server.send(200, "text/html", html_unconfigured_ap_root);
}

const char html_unconfigured_sta_root[] = { "\
<html\
<head>\
<title>Save Configuration</title>\
</head>\
<body>\
<p>Please save configuration:</p>\
<form method=\"POST\" action=\"/save_config\">\
<input type=\"submit\" value=\"Save\"/>\
</form>\
</body>\
</html>\
"
};

void handle_http_unconfigured_sta_get_root() {
  web_server.send(200, "text/html", html_unconfigured_sta_root);
}

void handle_http_unconfigured_get_root() {
  if (web_client_is_ap())
    handle_http_unconfigured_ap_get_root();
  else
    handle_http_unconfigured_sta_get_root();
}

void handle_http_unconfigured_ap_post_set_config() {
  if (!web_client_is_ap()) {
    web_server.send(409, "text/plain",
      "This URL may only be accessed from the access point WiFi network");
    return;
  }

  if (!web_server.hasArg("SSID")) {
    web_server.send(400, "text/plain", "SSID missing");
    return;
  }
  ssid = web_server.arg("SSID");
  if (!ssid.length()) {
    web_server.send(400, "text/plain", "SSID empty");
    return;
  }
  if (!str_isprint(ssid)) {
    web_server.send(400, "text/plain", "SSID invalid");
    return;
  }

  if (!web_server.hasArg("WIFIPW")) {
    web_server.send(400, "text/plain", "WiFi password missing");
    return;
  }
  wifipw = web_server.arg("WIFIPW");
  if (!wifipw.length()) {
    web_server.send(400, "text/plain", "WiFi password empty");
    return;
  }
  if (!str_isprint(wifipw)) {
    web_server.send(400, "text/plain", "WiFi password invalid");
    return;
  }

  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid.c_str(), wifipw.c_str());
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);

  web_server.send(200, "text/plain", "Station network initiated");
}

void handle_http_unconfigured_sta_post_save_config() {
  if (!web_client_is_sta()) {
    web_server.send(409, "text/plain",
      "This URL may only be accessed from the station WiFi network");
    return;
  }

  File f = SPIFFS.open("/config.txt", "w");
  f.println(ssid);
  f.println(wifipw);
  f.flush();
  f.close();
  SPIFFS.end();

  web_server.send(200, "text/plain", "Configuration Saved! Rebooting...");
  delay(1 * 1000);
  ESP.restart();
}

const char html_configured_sta_get_root[] = { "\
<html\
<head>\
<title>Door Control</title>\
</head>\
<body>"
#ifdef DOOR0_NAME
"<form method=\"POST\" action=\"/\">\
<input type=\"hidden\" name=\"door\" value=\"0\"/>\
<input type=\"submit\" value=\"Activate " DOOR0_NAME "\"/>\
</form>"
#ifdef PIN_ULTRA_TRIG_0
"State: STATE0<br/>"
"History: HIST0<br/>"
#endif
"<hr/>"
#endif
#ifdef DOOR1_NAME
"<form method=\"POST\" action=\"/\">\
<input type=\"hidden\" name=\"door\" value=\"1\"/>\
<input type=\"submit\" value=\"Activate " DOOR1_NAME "\"/>\
</form>"
#ifdef PIN_ULTRA_TRIG_1
"State: STATE1<br/>"
"History: HIST1<br/>"
#endif
"<hr/>"
#endif
"</body>\
</html>\
"
};

void handle_http_configured_get_root() {
  String s(html_configured_sta_get_root);
#ifdef PIN_ULTRA_TRIG_0
  s.replace("STATE0", ultra_echo_0.is_closed() ? "CLOSED" : "OPEN  ");
  s.replace("HIST0", ultra_echo_0.history_string());
#endif
#ifdef PIN_ULTRA_TRIG_1
  s.replace("STATE1", ultra_echo_1.is_closed() ? "CLOSED" : "OPEN  ");
  s.replace("HIST1", ultra_echo_1.history_string());
#endif
  web_server.send(200, "text/html", s);
}

void handle_http_configured_post_root() {
  if (!web_server.hasArg("door")) {
    web_server.send(400, "text/plain", "door missing");
    return;
  }

  String door_s = web_server.arg("door");
  int door = atoi(door_s.c_str());
#ifdef DOOR0_NAME
#define MIN_DOOR 0
#else
#define MIN_DOOR 1
#endif
#ifdef DOOR1_NAME
#define MAX_DOOR 1
#else
#define MAX_DOOR 0
#endif
  if ((door < MIN_DOOR) || (door > MAX_DOOR)) {
    web_server.send(400, "text/plain", "Door ID invalid");
    return;
  }
  web_server.sendHeader("Location", "/");
  web_server.send(302, "text/plain", "");

  RelayController *relay = &relay0;
#ifdef PIN_DOOR_1
  if (door)
    relay = &relay1;
#endif
  relay->trigger();
}

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println("");
  Serial.println("Running");

  pinMode(PIN_BTN, INPUT);
#ifdef PIN_DOOR_0
  digitalWrite(PIN_DOOR_0, LOW);
  pinMode(PIN_DOOR_0, OUTPUT);
#endif
#ifdef PIN_DOOR_1
  digitalWrite(PIN_DOOR_1, LOW);
  pinMode(PIN_DOOR_1, OUTPUT);
#endif

#ifdef PIN_ULTRA_TRIG_0
  digitalWrite(PIN_ULTRA_TRIG_0, LOW);
  pinMode(PIN_ULTRA_TRIG_0, OUTPUT);
  pinMode(PIN_ULTRA_ECHO_0, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_ULTRA_ECHO_0), isr_ultra_echo_0, CHANGE);
#endif
#ifdef PIN_ULTRA_TRIG_1
  digitalWrite(PIN_ULTRA_TRIG_1, LOW);
  pinMode(PIN_ULTRA_TRIG_1, OUTPUT);
  pinMode(PIN_ULTRA_ECHO_1, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_ULTRA_ECHO_1), isr_ultra_echo_1, CHANGE);
#endif

  SPIFFS.begin();
  if (SPIFFS.exists("/config.txt")) {
    File f = SPIFFS.open("/config.txt", "r");
    ssid = f.readStringUntil('\n');
    ssid.remove(ssid.length() - 1);
    wifipw = f.readStringUntil('\n');
    wifipw.remove(wifipw.length() - 1);
  }
  else {
    Serial.println("No /config.txt");
  }

  if (ssid.length() && wifipw.length()) {
    web_server.on("/", HTTP_GET, handle_http_configured_get_root);
    web_server.on("/", HTTP_POST, handle_http_configured_post_root);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), wifipw.c_str());
    WiFi.setAutoConnect(true);
    WiFi.setAutoReconnect(true);
  }
  else {
    web_server.on("/", HTTP_GET, handle_http_unconfigured_get_root);
    web_server.on("/set_config", HTTP_POST, handle_http_unconfigured_ap_post_set_config);
    web_server.on("/save_config", HTTP_POST, handle_http_unconfigured_sta_post_save_config);
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IPAddress(192, 168, 50, 1), IPAddress(192, 168, 50, 1), IPAddress(255, 255, 255, 0));
    WiFi.softAP("GarageDoor" DEVID, "GarageDoor");
  }

  MDNS.begin("garage-door" DEVID);
  MDNS.addService("http", "tcp", 80);
  LLMNR.begin("garage-door" DEVID);

  Serial.println("Starting HTTP server");
  web_server.onNotFound(handle_http_not_found);
  http_updater.setup(&web_server);
  web_server.begin();
}

void loop() {
  web_server.handleClient();

  static bool printed_ap_ip;
  if (!printed_ap_ip) {
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP().toString());
    printed_ap_ip = true;
  }

  static bool prev_sta_connected;
  bool new_sta_connected = WiFi.isConnected();
  if (prev_sta_connected != new_sta_connected) {
    prev_sta_connected = new_sta_connected;
    Serial.print("STA connected? ");
    Serial.print(prev_sta_connected);
    Serial.print("; IP=");
    Serial.println(WiFi.localIP().toString());
  }

  static unsigned long btn_pressed_time;
  if (btn_pressed_time) {
    if (digitalRead(PIN_BTN)) {
      btn_pressed_time = 0;
    } else {
      if ((millis() - btn_pressed_time) > (10 * 1000)) {
        Serial.println("Clearing configuration...");
        SPIFFS.format();
        Serial.println("Rebooting...");
        ESP.restart();
      }
    }
  } else {
    if (!digitalRead(PIN_BTN))
      btn_pressed_time = millis();
  }

  smtp_client.iter();
  smtp_queue.iter();
  relay0.iter();
#ifdef PIN_DOOR_1
  relay1.iter();
#endif

#if defined(PIN_ULTRA_TRIG_0) || defined(PIN_ULTRA_TRIG_1)
  static bool ultra_trigger_active;
  static unsigned long ultra_state_start;
  unsigned long t = millis();
  unsigned long ultra_state_elapsed = t - ultra_state_start;
  unsigned long ultra_max_state_time =
    ultra_trigger_active ? 10 : 2500;
  if (ultra_state_elapsed > ultra_max_state_time) {
    ultra_state_start = t;
    ultra_trigger_active = !ultra_trigger_active;
    if (ultra_trigger_active) {
#ifdef PIN_ULTRA_TRIG_0
      ultra_echo_0.pre_trigger();
#endif
#ifdef PIN_ULTRA_TRIG_1
      ultra_echo_1.pre_trigger();
#endif
    }
#if DEBUG_ULTRA
    Serial.printf("t %lu trig %d\n", t, (int)ultra_trigger_active);
#endif
#ifdef PIN_ULTRA_TRIG_0
    digitalWrite(PIN_ULTRA_TRIG_0, ultra_trigger_active);
#endif
#ifdef PIN_ULTRA_TRIG_1
    digitalWrite(PIN_ULTRA_TRIG_1, ultra_trigger_active);
#endif
  }
#endif
}
