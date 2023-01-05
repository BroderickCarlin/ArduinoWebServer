#include <SPI.h>
#include <Ethernet.h>
#include <EEPROM.h>

const byte MAX_CLIENTS = 2;
// length of "POST /msg " without a null terminating byte
const byte BUFFER_LEN = 10;
const int VIEW_COUNT_ADDR = 0;
// We will time out incoming requests after this long
const unsigned long REQUEST_TIMEOUT_MS = 5000;
// 1 hour in ms
const unsigned long HOURS_MS = 3600000;
// 1 minute in ms
const unsigned long MINUTES_MS = 60000;
// 1 second in ms
const unsigned long SECONDS_MS = 1000;
// Max message length
const int MAX_BODY_LEN = 144;
// Max supported header key len; len of "Content-Length"
const int MAX_HEADER_KEY_LEN = 14;
// Max supported header value len
const int MAX_HEADER_VALUE_LEN = 8;

// This is a randomly generated MAC 
// 68:4E:81:37:FF:BF
byte mac[] = {
  0x68, 0x4E, 0x81, 0x37, 0xFF, 0xBF
};

struct http_client {
  EthernetClient client;
  bool currentLineIsBlank;
  bool requestReceived;
  bool haveResponded;
  // This buffer is intentionally meant to only be large enough to 
  // hold the verb and path, specifically max "POST /msg "
  byte buffer[BUFFER_LEN];
  int buffer_idx;
  // A timestamp of when this client connected to us. This is used
  // to timeout a client
  unsigned long connectionTime;
  // The time at which we sent this client a response. This is used 
  // to provide a bit of a delay before closing the connection to make
  // sure our response gets to them
  unsigned long responseTime;

  // values used when reading headers
  char currentHeaderKey[MAX_HEADER_KEY_LEN];
  int header_key_idx;
  char currentHeaderValue[MAX_HEADER_VALUE_LEN];
  int header_value_idx;
  bool gotKey;

  // values used when reading the body
  char body[MAX_BODY_LEN];
  int body_idx;
  unsigned long bodyLen;
  bool readingBody;
};

// The server we are going to run, on port 80
EthernetServer server(80);

// A store of currently connected clients (max of 8)
http_client clients[MAX_CLIENTS];

// A variable for storing how many pages we've served (200s and 404s)
unsigned long g_visitor_count = 0;
unsigned long g_messge_count = 0;
String g_message = "";

char char_to_lower(char input) {
  if (input >= 'A' && input <= 'Z') {
    return input + 32;
  } else {
    return input;
  }
}

bool to_unsigned_long(char const *s, unsigned long *result, int len)
{
  if ( s == NULL || *s == '\0') {
    return false;
  }

  *result = 0;
  for(int i = 0; i < len; i++) {
    if ( s[i] < '0' || s[i] > '9' ) {
      return false;
    }

    *result = *result * 10 + (s[i] - '0');

    if (*result < 0) {
      return false;
    }
  }
  return true;
}

bool to_byte(char const *s, byte *result) {
  char a = char_to_lower(s[0]);
  char b = char_to_lower(s[1]);

  if (a >= '0' && a <= '9') {
    *result = (a - '0') << 4;
  } else if (a >= 'a' && a <= 'f') {
    *result = ((a - 'a') + 10) << 4;
  } else {
    return false;
  }

  if (b >= '0' && b <= '9') {
    *result |= (b - '0');
  } else if (b >= 'a' && b <= 'f') {
    *result |= (b - 'a') + 10;
  } else {
    return false;
  }
  return true;
}

bool set_message(char const *dirty, unsigned long len) {
  String local_str = "";

  // Start by going through and decoding the URL encoded input str
  unsigned long i = 0;
  for (; i < len; i++) {
    if (dirty[i] == '+') {
      local_str += ' ';
    } else if (dirty[i] == '%') {
      if (i + 2 >= len) {
        return false;
      } else {
        byte val;
        if (to_byte(&dirty[i + 1], &val)) {
          local_str += (char)val;
          i += 2;
        } else {
          return false;
        }
      }
    } else {
      local_str += (char)dirty[i];
    }
  }
  // Clear existing
  g_message = "";

  // Go to escaping 
  for (int i = 0; i < local_str.length(); i++) {
    if (local_str[i] == '<') {
      g_message += "&lt;";
    } else if (local_str[i] == '>') {
      g_message += "&gt;";
    } else if (local_str[i] == '&') {
      g_message += "&amp;";
    } else {
      g_message += local_str[i];
    }
  }
  
  return true;
}

// Send our HTML page with a 200 status code
void send_200(EthernetClient *client) {
  // Send a standard HTTP response and some headers
  client->println(F("HTTP/1.1 200 OK"));
  client->println(F("Content-Type: text/html"));
  client->println(F("Connection: close"));

  // Send a blank line indicating the start of the body
  client->println();

  // Send the actual HTML payload (aka the body)
  client->print(F("<!DOCTYPE html><title>Arduino go brrr</title><div style=text-align:center;font-family:Arial><div><h1>Welcome!</h1><p>This small web page is being served by an <a href=https://en.wikipedia.org/wiki/Arduino_Uno>Arduino Uno</a><p>This lil guy has serviced <b>"));
  client->print(g_visitor_count);
  client->print(F("</b> requests!</div><div style=margin:50px>"));
  if (g_message.length() != 0) {
    client->print(F("<h3>Message #"));
    client->print(g_messge_count);
    client->print(F(":</h3><p style=font-family:Monospace;color:#4c848b><b>"));
    client->print(g_message);
    client->print(F("</b></div>"));
  }
  client->println(F("<div><h3>Leave a message:</h3><form action=/msg method=post><input name=f> <input type=submit value=Submit></form></div></div><div style=text-align:center;position:absolute;bottom:25px;width:100%;height:100px><img height=100px src=https://i.imgur.com/qJ8VIaN.png></div>"));
}

void send_400(EthernetClient *client) {
  // Send a standard HTTP response and some headers
  client->println(F("HTTP/1.1 400 Bad Request"));
  client->println(F("Content-Type: text/html"));
  client->println(F("Connection: close"));

  // Send a blank line indicating the start of the body
  client->println();
}

void send_404(EthernetClient *client) {
  // Send a standard HTTP response and some headers
  client->println(F("HTTP/1.1 404 Not Found"));
  client->println(F("Content-Type: text/html"));
  client->println(F("Connection: close"));

  // Send a blank line indicating the start of the body
  client->println();

  // Send the actual HTML payload (aka the body)
  client->println(F("<!DOCTYPE html><title>Arduino is lost</title><style>#f{text-align:center;font-family:Arial}#b{text-align:center;position:absolute;bottom:25px;width:100%;height:100px}</style><div id=f><h1>404</h1><p>Somebody is lost</div><div id=b><img height=100px src=https://i.imgur.com/qJ8VIaN.png></div>"));
}

// Send a HTTP response with a 405 status code (Method not allowed)
void send_405(EthernetClient *client) {
  // Send a standard HTTP response and some headers
  client->println(F("HTTP/1.1 405 Method Not Allowed"));
  client->println(F("Content-Type: text/html"));
  client->println(F("Connection: close"));

  // Send a blank line indicating the start of the body
  client->println();
}

void check_new_clients() {
  EthernetClient newClient = server.accept();
  if (newClient) {
    for (byte i=0; i < MAX_CLIENTS; i++) {
      if (!clients[i].client) {
        clients[i].client = newClient;
        clients[i].currentLineIsBlank = true;
        clients[i].buffer_idx = 0;
        clients[i].requestReceived = false;
        clients[i].haveResponded = false;
        clients[i].connectionTime = millis();
        clients[i].header_key_idx = 0;
        clients[i].header_value_idx = 0;
        clients[i].gotKey = false;
        clients[i].body_idx = 0;
        clients[i].bodyLen = 0;
        clients[i].readingBody = false;

        memset(clients[i].currentHeaderKey, 0, MAX_HEADER_KEY_LEN);
        memset(clients[i].currentHeaderValue, 0, MAX_HEADER_VALUE_LEN);
        memset(clients[i].body, 0, MAX_BODY_LEN);
        break;
      }
    }
  }
}

void service_clients() {
  for (byte i=0; i < MAX_CLIENTS; i++) {
    if (clients[i].client) {
      while (clients[i].client.connected() && clients[i].client.available() > 0) {
        char c = clients[i].client.read();

        if (clients[i].readingBody) {
          clients[i].body[clients[i].body_idx] = c;
          clients[i].body_idx++;

          if (clients[i].body_idx == clients[i].bodyLen) {
            // Check if we have read the whole body
            clients[i].requestReceived = true;
            break;
          }
        } else {
          if (clients[i].buffer_idx < BUFFER_LEN) {
            // We don't know anything about this request yet so we need
            // to figure out the verb and path being requested against.
            clients[i].buffer[clients[i].buffer_idx] = c;
            clients[i].buffer_idx++;
          }

          if (c == '\n' && clients[i].currentLineIsBlank) {
            if (clients[i].bodyLen == 0) {
              // There is no body, so we are done reading the request!
              clients[i].requestReceived = true;
              break;
            } else {
              // We have some sort of body to read, so.... read it
              clients[i].readingBody = true;
            }
          }
          if (c == '\n') {
            if (memcmp(clients[i].currentHeaderKey, "content-length", 14) == 0) {
              // We got a header representing the content length!
              unsigned long len = 0;
              if (to_unsigned_long(clients[i].currentHeaderValue, &len, clients[i].header_value_idx)) {
                if (len < MAX_BODY_LEN) {
                  clients[i].bodyLen = len;
                } else {
                  clients[i].bodyLen = MAX_BODY_LEN;
                }
              }
            }

            // you're starting a new line
            clients[i].currentLineIsBlank = true;
            clients[i].header_key_idx = 0;
            clients[i].header_value_idx = 0;
            clients[i].gotKey = false;
            memset(clients[i].currentHeaderKey, 0, 10);
          } else if (c != '\r') {

            // you've gotten a character on the current line
            clients[i].currentLineIsBlank = false;

            if (c == ':' && !clients[i].gotKey) {
              clients[i].gotKey = true;
            } else if (clients[i].gotKey) {
              // We should add the current char to the value str
              if (c != ' ' || clients[i].header_value_idx != 0) {
                if (clients[i].header_value_idx != MAX_HEADER_VALUE_LEN) {
                  clients[i].currentHeaderValue[clients[i].header_value_idx] = c;
                  clients[i].header_value_idx++;
                }
              }
            } else {
              // We should add the current char to the key str
              if (clients[i].header_key_idx != MAX_HEADER_KEY_LEN) {
                clients[i].currentHeaderKey[clients[i].header_key_idx] = char_to_lower(c);
                clients[i].header_key_idx++;
              }
            }
          }
        }
      }
    }
  }
}

void send_responses() {
  for (byte i=0; i < MAX_CLIENTS; i++) {
    if (clients[i].client && clients[i].requestReceived && !clients[i].haveResponded) {
      // The client is still connected and we received a full request; handle it!
      if (clients[i].buffer_idx != BUFFER_LEN) {
        // Something has gone terribly wrong as we didn't even get the start of an HTTP
        // frame from this client - return a 400
        send_400(&clients[i].client);
      } else if (memcmp(clients[i].buffer, "GET / ", 6) == 0) {
        // We got a GET request for the root - return a 200
        send_200(&clients[i].client);
      } else if (memcmp(clients[i].buffer, "POST /msg ", 9) == 0) {
        // We got a POST with our potential new message!
        if (memcmp(clients[i].body, "f=", 2) == 0 && set_message(&clients[i].body[2], clients[i].bodyLen - 2)) {
          g_messge_count++;
          send_200(&clients[i].client);
        } else {
          // TODO: probably return something diff here
          send_404(&clients[i].client);
        }
      } else if (memcmp(clients[i].buffer, "GET ", 4) == 0) {
        // We got a GET, but it wasn't for the root - return a 404
        send_404(&clients[i].client);
      } else {
        // We got something besides a GET, return a 405
        send_405(&clients[i].client);
      }

      // We'll record any response as a visitor cause why not
      g_visitor_count++;

      // Record when we responded to this client
      clients[i].responseTime = millis();
      clients[i].haveResponded = true;
    }
  }
}

void kill_clients() {
  unsigned long now = millis();
  for (byte i=0; i < MAX_CLIENTS; i++) {
    if (clients[i].client) {
      if (!clients[i].client.connected()) {
        // Clean up a client that disconnected
        clients[i].client.stop();
      } else if (clients[i].requestReceived && clients[i].responseTime != now) {
        // Clean up a client that we responded to
        clients[i].client.stop();
      } else if (now - clients[i].connectionTime > REQUEST_TIMEOUT_MS) {
        // Clean up a client that has timedout on its request
        clients[i].client.stop();
      }
    }
  }
}

byte calc_checksum(byte *buf, int len) {
  byte sum = 0;
  for (int i=0; i < len; i++) {
    sum ^= buf[i];
  }
  return sum;
}

void save_view_count() {
  byte chksum = calc_checksum((byte*) &g_visitor_count, sizeof(g_visitor_count));
  // Write the value to the EEPROM
  EEPROM.put(VIEW_COUNT_ADDR, g_visitor_count);
  // Write the checksum to the EEPROM
  EEPROM.put(VIEW_COUNT_ADDR + sizeof(g_visitor_count), chksum);
}

void setup_eeprom() {
  byte chksum;
  // Load the viewcount from the EEPROM
  EEPROM.get(VIEW_COUNT_ADDR, g_visitor_count);
  // Load the chksum byte from the EEPROM
  EEPROM.get(VIEW_COUNT_ADDR + sizeof(g_visitor_count), chksum);

  // Verify the data from the EEPROM is valid
  if (calc_checksum((byte*) &g_visitor_count, sizeof(g_visitor_count)) != chksum) {
    // It doesn't appear the data in the EEPROM was valid; overwrite it
    g_visitor_count = 0;
    save_view_count();
  }
}

// A method that gets called every hour
void hour_tick() {
  save_view_count();
}

// A method that gets called every minute
void minute_tick() {

}

// A method that gets called every second
void second_tick() {
  // Keep up our DHCP lease
  Ethernet.maintain();
}

void setup() {
  Ethernet.init(10);

  setup_eeprom();

  if (Ethernet.begin(mac) == 0) {
    // no point in carrying on, so do nothing forevermore:
    while (true) {
      delay(1);
    }
  }
}

void loop() {
  static unsigned long last_hour_tick = millis();
  static unsigned long last_minute_tick = millis();
  static unsigned long last_second_tick = millis();
  unsigned long now = millis();

  // check for any new client connecting
  check_new_clients();

  // check for incoming data from all clients
  service_clients();

  // send out potential HTTP responses
  send_responses();

  // stop any clients which we no longer want connected
  kill_clients();

  // check if it is time to perform our periodic ticks
  if (now - last_hour_tick > HOURS_MS) {
    last_hour_tick = now;
    hour_tick();
  }
  if (now - last_minute_tick > MINUTES_MS) {
    last_minute_tick = now;
    minute_tick();
  }
  if (now - last_second_tick > SECONDS_MS) {
    last_second_tick = now;
    second_tick();
  }
}
