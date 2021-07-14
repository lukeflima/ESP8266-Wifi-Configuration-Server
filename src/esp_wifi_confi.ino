#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <FS.h>

const char* ESP_default_ssid = "ESP8266";
//const char* default_password = "password";

IPAddress apIP(192, 168, 1, 1);
IPAddress subnetIP(255,255,255,0);

const byte DNS_PORT = 53;
DNSServer dnsServer;
AsyncWebServer server(80);

bool wifi_configured = false;
bool file_created = false;
bool set_timer = false;

String ssid, password;

const char config_index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
</head>
<body>
  <h2>ESP01 Wifi Config</h2>
  <form action="/set_network" method="POST">
    <label for="ssid">SSID: </label>
    <select id="ssid" name="ssid"></select>
    <label for="password">Password: </label>
    <input type="text" id="password" name="password"/>
    <button type="submit">Save</button>
  </form>
</body>
<script>
const ssid_select = document.getElementById("ssid");
const get_network_list = () => { 
    fetch("/scan").then((res) => res.json()).then((json) => {
        while (ssid_select.firstChild) ssid_select.removeChild(ssid_select.lastChild);
        json.map(network => {
            const opt = document.createElement('option');
            opt.value = network.ssid;
            opt.innerHTML = network.ssid;
            ssid_select.appendChild(opt);
        })
    })
}

get_network_list();
setTimeout(get_network_list, 10000);

</script>
</html>)rawliteral";


void notFound(AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
}

void connect_to_configured_wifi() {
  File f = SPIFFS.open("/wifi.conf", "r");
  if (!f) {
    //Serial.println("No wifi configuration file");
    file_created = false;
  } else {
    wifi_configured = true;
    ssid = f.readStringUntil('\n');
    password = f.readStringUntil('\n');
    f.close();

    WiFi.softAPdisconnect();
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    delay(1000);
    Serial.println("Connecting to " + ssid);
    WiFi.begin(ssid, password);
    
    Serial.print("Connecting");
    int connection_try_timeout = 60; // 30 seconds timeout
    int connection_timeout = 0;
    while (WiFi.status() != WL_CONNECTED)
    {
      delay(500);
      Serial.print(".");
      connection_timeout += 1;
      if(connection_timeout == connection_try_timeout) {
        SPIFFS.remove("/wifi.conf");
        wifi_configured = false;
        file_created = false;
        break;
      }
    }
    if(wifi_configured) {
      Serial.println("");
      Serial.println("Connected to " + ssid);
    } else {
      Serial.println("");
      Serial.println("Connection timeout");
      configure_ap_wifi_and_dns();
    }
  }
}

void configure_ap_wifi_and_dns() {
    Serial.print("Setting AP (Access Point)…");

    WiFi.mode(WIFI_AP);
    delay(1000);
    WiFi.softAPConfig(apIP, apIP, subnetIP);
    WiFi.softAP(ESP_default_ssid);
  
    WiFi.scanNetworks(true);
    
    // modify TTL associated  with the domain name (in seconds)
    // default is 60 seconds
    dnsServer.setTTL(300);
    // set which return code will be used for all other domains (e.g. sending
    // ServerFailure instead of NonExistentDomain will reduce number of queries
    // sent by clients)
    // default is DNSReplyCode::NonExistentDomain
    dnsServer.setErrorReplyCode(DNSReplyCode::ServerFailure);
  
    // start DNS server for a specific domain name
    dnsServer.start(DNS_PORT, "www.wificonfig.info", apIP);
}

void setup(void)
{
    // Serial port for debugging purposes
  Serial.begin(115200);
  SPIFFS.begin();

  //SPIFFS.remove("/wifi.conf");

  File f = SPIFFS.open("/wifi.conf", "r");
  if (!f) {
    Serial.println("No wifi configuration file");
    file_created = false;

    configure_ap_wifi_and_dns();
  } else {
    Serial.println("Wifi configuration file present");
    file_created = true;
    f.close();
    
    connect_to_configured_wifi();
  }

  // Route for root / web page
  server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "[";
    int n = WiFi.scanComplete();
    if(n == -2){
      WiFi.scanNetworks(true);
    } else if(n){
      for (int i = 0; i < n; ++i){
        if(i) json += ",";
        json += "{";
        json += "\"rssi\":"+String(WiFi.RSSI(i));
        json += ",\"ssid\":\""+WiFi.SSID(i)+"\"";
        json += ",\"bssid\":\""+WiFi.BSSIDstr(i)+"\"";
        json += ",\"channel\":"+String(WiFi.channel(i));
        json += ",\"secure\":"+String(WiFi.encryptionType(i));
        json += ",\"hidden\":"+String(WiFi.isHidden(i)?"true":"false");
        json += "}";
      }
      WiFi.scanDelete();
      if(WiFi.scanComplete() == -2){
        WiFi.scanNetworks(true);
      }
    }
    json += "]";
    request->send(200, "application/json", json);
    json = String();
  });

  server.on("/set_network", HTTP_POST, [](AsyncWebServerRequest *request){
    String ssid, password, response;
    if(request->hasParam("ssid", true) && request->hasParam("password", true)) {
      ssid =  request->getParam("ssid", true)->value();
      password =  request->getParam("password", true)->value();
      response = "{\"ssid\":\"";
      response += ssid;
      response += "\"}";

      File f = SPIFFS.open("/wifi.conf", "w");
      if (!f) {
          Serial.println("file open failed");
          request->send(200, "application/json", "{\"error\":\"Couldn't open file\"}");
      }else {
        request->send(200, "application/json", response);
        f.print(ssid);
        f.print("\n");
        f.print(password); 
        f.print("\n"); 
        f.close();
        file_created = true;
      }
    }

    server.onNotFound(notFound);
    
    ssid = String();
    password = String();
    response = String();
  });

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    String res;
    if(wifi_configured){
      res = "<h1>Connect to " + ssid + "</h1>";
      request->send(200, "text/html", res);
    }else {
      request->send(200, "text/html", config_index_html);
    }
  });


  // Start server
  server.begin();
}

void loop() {
  if(!wifi_configured) dnsServer.processNextRequest();
  if(file_created && !wifi_configured){
      connect_to_configured_wifi();
  }
  if(file_created && wifi_configured) {
    // Wifi configured
  }
}
