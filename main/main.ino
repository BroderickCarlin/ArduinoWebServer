#include <SPI.h>
#include <Ethernet.h>
#include <EEPROM.h>

// This is the HTML doc we are going to return, split into 2 halves
// a variable value will be inserted inbetween the 2 strings for each request
const char HTML_START[] = "<!doctypehtml><title>Arduino go brrr</title><style>#f{text-align:center;font-family:Arial,Helvetica,sans-serif}#b{text-align:center;position:absolute;bottom:25px;width:100%;height:100px}</style><div id=f><h1>Welcome!</h1><p>This small web page is being served by an <a href=https://en.wikipedia.org/wiki/Arduino_Uno>Arduino Uno</a><p>This lil guy has served <b>";
const char HTML_END[] = "</b> pages!</div><div id=b><img height=100px src=https://i.imgur.com/qJ8VIaN.png></div>";
// This is the HTML doc we are going to return on 404's
const char HTML_404[] = "<!doctypehtml><title>Arduino is lost</title><style>#f{text-align:center;font-family:Arial,Helvetica,sans-serif}#b{text-align:center;position:absolute;bottom:25px;width:100%;height:100px}</style><div id=f><h1>404</h1><p>Somebody is lost</div><div id=b><img height=100px src=https://i.imgur.com/qJ8VIaN.png></div>";

const byte MAX_CLIENTS = 8;
// length of "GET / " without a null terminating byte
const byte BUFFER_LEN = 6;
const int VIEW_COUNT_ADDR = 0;
// We will time out incoming requests after this long
const unsigned long REQUEST_TIMEOUT_MS = 5000;
// 1 hour in ms
const unsigned long HOURS_MS = 3600000;
// 1 minute in ms
const unsigned long MINUTES_MS = 60000;
// 1 second in ms
const unsigned long SECONDS_MS = 1000;

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
  // hold the verb and path, specifically "GET / "
  byte buffer[BUFFER_LEN];
  byte buffer_idx;
  // A timestamp of when this client connected to us. This is used
  // to timeout a client
  unsigned long connectionTime;
  // The time at which we sent this client a response. This is used 
  // to provide a bit of a delay before closing the connection to make
  // sure our response gets to them
  unsigned long responseTime;
};

// The server we are going to run, on port 80
EthernetServer server(80);

// A store of currently connected clients (max of 8)
http_client clients[MAX_CLIENTS];

// A variable for storing how many pages we've served (200s and 404s)
unsigned long g_visitor_count = 0;

// Send our HTML page with a 200 status code
void send_200(EthernetClient *client) {
  // Send a standard HTTP response and some headers
  client->println("HTTP/1.1 200 OK");
  client->println("Content-Type: text/html");
  client->println("Connection: close");

  // Send a blank line indicating the start of the body
  client->println();

  // Send the actual HTML payload (aka the body)
  client->print(HTML_START);
  client->print(g_visitor_count);
  client->println(HTML_END);
}

void send_400(EthernetClient *client) {
  // Send a standard HTTP response and some headers
  client->println("HTTP/1.1 400 Bad Request");
  client->println("Content-Type: text/html");
  client->println("Connection: close");

  // Send a blank line indicating the start of the body
  client->println();

  // Send the actual HTML payload (aka the body)
  client->println(HTML_404);
}

void send_404(EthernetClient *client) {
  // Send a standard HTTP response and some headers
  client->println("HTTP/1.1 404 Not Found");
  client->println("Content-Type: text/html");
  client->println("Connection: close");

  // Send a blank line indicating the start of the body
  client->println();

  // Send the actual HTML payload (aka the body)
  client->println(HTML_404);
}

// Send a HTTP response with a 405 status code (Method not allowed)
void send_405(EthernetClient *client) {
  // Send a standard HTTP response and some headers
  client->println("HTTP/1.1 405 Method Not Allowed");
  client->println("Content-Type: text/html");
  client->println("Connection: close");

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

        if (clients[i].buffer_idx < BUFFER_LEN) {
          // We don't know anything about this request yet so we need
          // to figure out the verb and path being requested against.
          // For simplicity sake, we only support a GET on the root 
          // so we are just going to check for that and error out on
          // anything else
          clients[i].buffer[clients[i].buffer_idx] = c;
          clients[i].buffer_idx++;
        }

        if (c == '\n' && clients[i].currentLineIsBlank) {
          clients[i].requestReceived = true;
          break;
        }
        if (c == '\n') {
          // you're starting a new line
          clients[i].currentLineIsBlank = true;
        } else if (c != '\r') {
          // you've gotten a character on the current line
          clients[i].currentLineIsBlank = false;
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
      } else if (memcmp(clients[i].buffer, "GET / ", BUFFER_LEN) == 0) {
        // We got a GET request for the root - return a 200
        send_200(&clients[i].client);
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
