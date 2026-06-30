#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <esp_wifi.h>
#include <esp_bt.h>
#include <esp_gap_ble_api.h>
#include "USB.h"
USBCDC my_usb;

// ======================================================
// STATE
// ======================================================
BLEServer *pServer = nullptr;
bool bleRunning = false;
String inputBuffer;

bool wifiEnabled = false;
String bleName = "ESP32_TEST";

// WiFi RF settings (aplicados em WIFI AP / WIFI STA)
uint8_t wifiProtoMask = WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N;
wifi_bandwidth_t wifiBandwidth = WIFI_BW_HT20;
String wifiProtoStr = "BGN";
int wifiBandMHz = 20;

// BLE RF settings
esp_ble_adv_channel_t bleAdvChannel = ADV_CHNL_ALL;
String bleAdvChannelStr = "ALL";
int blePhyMode = 1; // 1=LE 1M (1 MHz)  2=LE 2M (2 MHz)

void serial_write(String data)
{
    Serial.println(data);
    my_usb.println(data);
}

// ======================================================
// UTIL (ENGINEERING LEVEL)
// ======================================================
String upper(String s)
{
    s.toUpperCase();
    return s;
}

void ok(String msg = "")
{
    serial_write("[OK] " + msg);
}

void err(String msg)
{
    serial_write("[ERROR] " + msg);
}

void info(String msg)
{
    serial_write("[INFO] " + msg);
}

bool hasArgs(int count, String raw, String cmd)
{
    int c = 0;
    for (int i = cmd.length(); i < raw.length(); i++)
    {
        if (raw[i] == ' ')
            c++;
    }
    return c >= count;
}

// ======================================================
// WIFI CONTROL (RF TEST MODE)
// ======================================================
void wifiOff()
{
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    wifiEnabled = false;
    ok("WiFi disabled");
}

void wifiSetCountry(String cc)
{
    if (cc.length() < 2)
    {
        err("Country must be 2 letters (BR/US/CN)");
        return;
    }

    wifi_country_t country = {
        .cc = {cc[0], cc[1]},
        .schan = 1,
        .nchan = 13,
        .max_tx_power = 84,
        .policy = WIFI_COUNTRY_POLICY_AUTO};

    esp_wifi_set_country(&country);
    ok("Country set to " + cc);
}

void wifiSetPower(float dbm)
{
    WiFi.setTxPower((wifi_power_t)dbm);
    ok("TX power set to " + String(dbm) + " dBm (approx)");
}

void wifiSetProto(String proto)
{
    proto.trim();
    proto.toUpperCase();

    if (proto == "B")
    {
        wifiProtoMask = WIFI_PROTOCOL_11B;
        wifiProtoStr = "B";
    }
    else if (proto == "G")
    {
        wifiProtoMask = WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G;
        wifiProtoStr = "G";
    }
    else if (proto == "N")
    {
        wifiProtoMask = WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N;
        wifiProtoStr = "N";
    }
    else
    {
        err("Invalid proto. Use: B / G / N");
        return;
    }

    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) == ESP_OK)
    {
        if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA)
            esp_wifi_set_protocol(WIFI_IF_AP, wifiProtoMask);
        if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA)
            esp_wifi_set_protocol(WIFI_IF_STA, wifiProtoMask);
    }

    ok("WiFi protocol set to 802.11" + wifiProtoStr);
}

void wifiSetBand(int bw)
{
    if (bw == 20)
    {
        wifiBandwidth = WIFI_BW_HT20;
        wifiBandMHz = 20;
    }
    else if (bw == 40)
    {
        wifiBandwidth = WIFI_BW_HT40;
        wifiBandMHz = 40;
    }
    else
    {
        err("Invalid bandwidth. Use: 20 or 40");
        return;
    }

    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) == ESP_OK)
    {
        if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA)
            esp_wifi_set_bandwidth(WIFI_IF_AP, wifiBandwidth);
        if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA)
            esp_wifi_set_bandwidth(WIFI_IF_STA, wifiBandwidth);
    }

    ok("WiFi bandwidth set to " + String(wifiBandMHz) + " MHz");
}

void wifiAP(String ssid, String pass, int ch, bool hidden)
{
    if (ssid.length() == 0)
    {
        err("SSID empty");
        return;
    }

    WiFi.mode(WIFI_AP);
    esp_wifi_set_protocol(WIFI_IF_AP, wifiProtoMask);
    esp_wifi_set_bandwidth(WIFI_IF_AP, wifiBandwidth);

    bool res = WiFi.softAP(
        ssid.c_str(),
        pass.length() ? pass.c_str() : nullptr,
        ch,
        hidden ? 1 : 0,
        8);

    if (!res)
    {
        err("AP start failed");
        return;
    }

    wifiEnabled = true;
    ok("AP started SSID=" + ssid + " CH=" + String(ch));
}
// ======================================================
// WIFI STA (HOMOLOGATION ROBUST CONNECT TEST)
// ======================================================

bool wifiWait(uint32_t timeout_ms)
{
    uint32_t start = millis();

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(200);

        if (millis() - start > timeout_ms)
        {
            return false;
        }
    }

    return true;
}

void wifiSTA(String ssid, String pass)
{
    if (ssid.length() == 0)
    {
        err("SSID empty");
        serial_write("[RESULT] WIFI_STA = FAIL");
        serial_write("[DETAIL] missing SSID");
        return;
    }

    info("WIFI STA TEST START");
    info("SSID = " + ssid);

    // garante estado limpo de rádio antes do teste
    WiFi.disconnect(true, true);
    delay(300);

    WiFi.mode(WIFI_STA);
    esp_wifi_set_protocol(WIFI_IF_STA, wifiProtoMask);
    esp_wifi_set_bandwidth(WIFI_IF_STA, wifiBandwidth);
    WiFi.begin(ssid.c_str(), pass.c_str());

    info("Connecting... timeout = 10s");

    bool connected = wifiWait(10000);

    if (!connected)
    {
        err("Connection timeout");
        serial_write("[RESULT] WIFI_STA = FAIL");
        serial_write("[DETAIL] association/auth timeout");
        return;
    }

    // validação extra (muito importante em homologação)
    if (WiFi.localIP().toString() == "0.0.0.0")
    {
        err("No DHCP lease");
        serial_write("[RESULT] WIFI_STA = FAIL");
        serial_write("[DETAIL] connected but no IP");
        return;
    }

    // leitura final de RF real
    int rssi = WiFi.RSSI();
    int ch = WiFi.channel();

    serial_write("\n[RESULT] WIFI_STA = PASS");

    serial_write("[WIFI STA REPORT]");
    serial_write("STATUS : CONNECTED");
    serial_write("SSID   : " + ssid);
    serial_write("IP     : " + WiFi.localIP().toString());
    serial_write("RSSI   : " + String(rssi));
    serial_write("CHANNEL: " + String(ch));

    ok("STA test completed successfully");
}

void wifiScan()
{
    info("Scanning WiFi...");
    int n = WiFi.scanNetworks();

    if (n <= 0)
    {
        err("No networks found");
        return;
    }

    for (int i = 0; i < n; i++)
    {
        serial_write("[WIFI] " + String(i + 1) + ") " + WiFi.SSID(i) + " | RSSI " + String(WiFi.RSSI(i)) + " | CH " + String(WiFi.channel(i)));
    }

    ok("Scan complete");
}

// ======================================================
// BLE CONTROL (REAL BEHAVIOR EXPLAINED)
// ======================================================
//
// BLE ESP32 LIMITATIONS:
// - NO manual channel control (uses frequency hopping)
// - TX power is discrete levels (not real dBm precision)
// - main RF mode is advertising + optional GATT
// ======================================================

void bleStart(String name)
{
    if (name.length() == 0)
    {
        err("BLE name empty");
        return;
    }

    bleName = name;

    BLEDevice::init(name.c_str());
    pServer = BLEDevice::createServer();

    BLEService *service =
        pServer->createService("12345678-1234-1234-1234-1234567890ab");

    BLECharacteristic *ch = service->createCharacteristic(
        "abcd",
        BLECharacteristic::PROPERTY_READ |
            BLECharacteristic::PROPERTY_WRITE);

    ch->setValue("ANATEL_TEST");

    service->start();

    BLEAdvertising *adv = BLEDevice::getAdvertising();

    adv->addServiceUUID(service->getUUID());

    // default RF test values
    adv->setMinInterval(0x20); // ~20ms
    adv->setMaxInterval(0x40); // ~40ms
    adv->setAdvertisementChannelMap(bleAdvChannel);

    adv->start();

    bleRunning = true;

    {
        esp_ble_gap_phy_t phyType =
            (blePhyMode == 2) ? ESP_BLE_GAP_PHY_2M : ESP_BLE_GAP_PHY_1M;
        esp_ble_gap_set_preferred_default_phy(phyType, phyType);
    }

    ok("BLE advertising started (" + name + ")");
}

void bleStop()
{
    if (!bleRunning)
    {
        err("BLE already OFF");
        return;
    }

    BLEDevice::deinit(true);
    bleRunning = false;
    pServer = nullptr;

    ok("BLE stopped");
}

void bleSetPower(int level)
{
    esp_ble_tx_power_set(
        ESP_BLE_PWR_TYPE_ADV,
        (esp_power_level_t)level);

    ok("BLE TX power updated (level=" + String(level) + ")");
}

void bleSetInterval(int min_ms, int max_ms)
{
    BLEAdvertising *adv = BLEDevice::getAdvertising();

    adv->setMinInterval(min_ms);
    adv->setMaxInterval(max_ms);

    ok("BLE interval updated (" +
       String(min_ms) + "-" + String(max_ms) + ")");
}

void bleSetChannel(String chanStr)
{
    chanStr.trim();
    chanStr.toUpperCase();

    if (chanStr == "37")
    {
        bleAdvChannel = ADV_CHNL_37;
        bleAdvChannelStr = "37";
    }
    else if (chanStr == "38")
    {
        bleAdvChannel = ADV_CHNL_38;
        bleAdvChannelStr = "38";
    }
    else if (chanStr == "39")
    {
        bleAdvChannel = ADV_CHNL_39;
        bleAdvChannelStr = "39";
    }
    else if (chanStr == "ALL")
    {
        bleAdvChannel = ADV_CHNL_ALL;
        bleAdvChannelStr = "ALL";
    }
    else
    {
        err("Invalid channel. Use: 37 / 38 / 39 / ALL");
        return;
    }

    if (bleRunning)
    {
        BLEAdvertising *adv = BLEDevice::getAdvertising();
        adv->setAdvertisementChannelMap(bleAdvChannel);
    }

    ok("BLE advertising channel: " + bleAdvChannelStr +
       (bleRunning ? "" : " [applied at next BLE ON]"));
}

void bleSetPHY(int phy)
{
    if (phy != 1 && phy != 2)
    {
        err("Invalid PHY. Use: 1 (LE 1M / 1 MHz) or 2 (LE 2M / 2 MHz)");
        return;
    }

    blePhyMode = phy;

    if (bleRunning)
    {
        esp_ble_gap_phy_t phyType =
            (phy == 2) ? ESP_BLE_GAP_PHY_2M : ESP_BLE_GAP_PHY_1M;
        esp_ble_gap_set_preferred_default_phy(phyType, phyType);
    }

    ok("BLE PHY set to LE " + String(phy) + "M (" + String(phy) + " MHz)" +
       (bleRunning ? "" : " [applied at next BLE ON]"));
}

void bleStatus()
{
    serial_write("\n[BLE STATUS]");
    serial_write("STATE  : " + String(bleRunning ? "ON" : "OFF"));
    serial_write("NAME   : " + bleName);
    serial_write("CHANNEL: " + bleAdvChannelStr + " (adv channel map)");
    serial_write("PHY    : LE " + String(blePhyMode) + "M (" + String(blePhyMode) + " MHz)");

    serial_write("RF NOTES:");
    serial_write("- Adv channels 37/38/39 sao os canais primarios BLE");
    serial_write("- Canais de dados 0-36 usam AFH hopping (automatico)");
    serial_write("- PHY: 1M=BLE 4.x compat | 2M=BLE 5.0 (2 MHz largura)");
    serial_write("- TX power e por steps, nao dBm continuo");
}

// ======================================================
// WIFI STATUS
// ======================================================
void wifiStatus()
{
    serial_write("\n[WIFI STATUS]");
    serial_write("MODE    : " + String(WiFi.getMode()));
    serial_write("PROTO   : 802.11" + wifiProtoStr);
    serial_write("BAND    : " + String(wifiBandMHz) + " MHz");
    serial_write("IP      : " + WiFi.localIP().toString());
    serial_write("RSSI    : " + String(WiFi.RSSI()));
    serial_write("CHANNEL : " + String(WiFi.channel()));
}

// ======================================================
// HELP (ENGINEERING / HOMOLOGATION LEVEL)
// ======================================================
void printHelp()
{
    serial_write("\n==================================================");
    serial_write(" ESP32 RF TEST FIRMWARE - ENGINEERING MODE");
    serial_write("==================================================");

    serial_write("\nWIFI COMMANDS:");
    serial_write("  WIFI OFF");
    serial_write("    -> desliga o radio WiFi completamente");

    serial_write("  WIFI AP <ssid> <pass> <channel> <hidden 0/1>");
    serial_write("    -> inicia Access Point (teste RF canal fixo)");

    serial_write("  WIFI STA <ssid> <pass>");
    serial_write("    -> modo station (teste de associacao RF)");

    serial_write("  WIFI SCAN");
    serial_write("    -> varre redes vizinhas (teste receptor)");

    serial_write("  WIFI PROTO <B|G|N>");
    serial_write("    -> seleciona tecnologia 802.11:");
    serial_write("       B = 802.11b        (2.4 GHz, DSSS, ate 11 Mbps)");
    serial_write("       G = 802.11b+g      (2.4 GHz, OFDM, ate 54 Mbps)");
    serial_write("       N = 802.11b+g+n    (2.4 GHz, HT OFDM, ate 150 Mbps)");

    serial_write("  WIFI BAND <20|40>");
    serial_write("    -> largura do canal em MHz:");
    serial_write("       20 = HT20 (canal unico, 20 MHz)");
    serial_write("       40 = HT40 (canal bonded, 40 MHz, apenas 802.11n)");

    serial_write("  WIFI POWER <0.0-20.5>");
    serial_write("    -> ajuste de potencia TX (dBm aprox)");

    serial_write("  WIFI COUNTRY <BR|US|CN>");
    serial_write("    -> dominio regulatorio (limites de canal e potencia)");

    serial_write("\nBLE COMMANDS:");
    serial_write("  BLE ON <name>");
    serial_write("    -> inicia advertising + perfil GATT minimo");

    serial_write("  BLE OFF");
    serial_write("    -> para stack BLE completamente");

    serial_write("  BLE POWER <level>");
    serial_write("    niveis: -12 -9 -6 -3 0 +3 +6 +9");
    serial_write("    -> potencia TX por steps (controlado por hardware)");

    serial_write("  BLE ADV <min> <max>");
    serial_write("    -> intervalo de advertising (unidades ~0.625 ms)");

    serial_write("  BLE CHAN <37|38|39|ALL>");
    serial_write("    -> seleciona canal(is) primario(s) de advertising:");
    serial_write("       37  = 2402 MHz");
    serial_write("       38  = 2426 MHz");
    serial_write("       39  = 2480 MHz");
    serial_write("       ALL = todos os tres canais (padrao)");
    serial_write("       (canais de dados 0-36 usam AFH - nao configuravel)");

    serial_write("  BLE PHY <1|2>");
    serial_write("    -> seleciona PHY BLE (taxa de dados / largura de banda):");
    serial_write("       1 = LE 1M  (1 Mbps, ~1 MHz de largura, BLE 4.x)");
    serial_write("       2 = LE 2M  (2 Mbps, ~2 MHz de largura, BLE 5.0)");

    serial_write("\nSYSTEM:");
    serial_write("  STATUS  -> dump completo do estado RF");
    serial_write("  RESET   -> reboot do dispositivo");
    serial_write("  HELP    -> este menu");

    serial_write("\nNOTAS:");
    serial_write("- WIFI PROTO e WIFI BAND aplicados no proximo WIFI AP/STA");
    serial_write("  (ou imediatamente se WiFi ja estiver ativo)");
    serial_write("- BLE CHAN e BLE PHY aplicados no proximo BLE ON");
    serial_write("  (ou imediatamente se BLE ja estiver rodando)");
    serial_write("- Canais de dados BLE (0-36) usam AFH, nao sao selecionaveis");

    serial_write("==================================================\n");
}

// ======================================================
// STATUS
// ======================================================
void systemStatus()
{
    serial_write("\n=========== SYSTEM STATUS ===========");
    wifiStatus();
    bleStatus();
    serial_write("====================================\n");
}

// ======================================================
// PARSER (ROBUST ENGINEERING VERSION)
// ======================================================
String arg(String raw, int index)
{
    int start = 0, found = 0;

    for (int i = 0; i < raw.length(); i++)
    {
        if (raw[i] == ' ')
        {
            if (found == index)
                return raw.substring(start, i);
            found++;
            start = i + 1;
        }
    }

    if (found == index)
        return raw.substring(start);
    return "";
}

void handleCommand(String raw)
{
    raw.trim();
    String cmd = upper(raw);

    if (cmd == "HELP")
    {
        printHelp();
    }

    else if (cmd == "WIFI OFF")
    {
        wifiOff();
    }

    else if (cmd == "WIFI SCAN")
    {
        wifiScan();
    }

    else if (cmd.startsWith("WIFI AP"))
    {
        wifiAP(arg(raw, 2), arg(raw, 3), arg(raw, 4).toInt(), arg(raw, 5).toInt());
    }

    else if (cmd.startsWith("WIFI STA"))
    {
        wifiSTA(arg(raw, 2), arg(raw, 3));
    }

    else if (cmd.startsWith("WIFI POWER"))
    {
        wifiSetPower(arg(raw, 2).toFloat());
    }

    else if (cmd.startsWith("WIFI COUNTRY"))
    {
        wifiSetCountry(arg(raw, 2));
    }

    else if (cmd.startsWith("WIFI PROTO"))
    {
        wifiSetProto(arg(raw, 2));
    }

    else if (cmd.startsWith("WIFI BAND"))
    {
        wifiSetBand(arg(raw, 2).toInt());
    }

    else if (cmd.startsWith("BLE ON"))
    {
        bleStart(arg(raw, 2));
    }

    else if (cmd == "BLE OFF")
    {
        bleStop();
    }

    else if (cmd.startsWith("BLE POWER"))
    {
        bleSetPower(arg(raw, 2).toInt());
    }

    else if (cmd.startsWith("BLE ADV"))
    {
        bleSetInterval(arg(raw, 2).toInt(), arg(raw, 3).toInt());
    }

    else if (cmd.startsWith("BLE CHAN"))
    {
        bleSetChannel(arg(raw, 2));
    }

    else if (cmd.startsWith("BLE PHY"))
    {
        bleSetPHY(arg(raw, 2).toInt());
    }

    else if (cmd == "STATUS")
    {
        systemStatus();
    }

    else if (cmd == "RESET")
    {
        info("Rebooting...");
        delay(500);
        ESP.restart();
    }

    else
    {
        err("Unknown command");
        info("Type HELP for full command reference");
    }
}

// ======================================================
// SETUP
// ======================================================
void setup()
{
    Serial.begin(115200);
    USB.VID(0x0001);
    USB.PID(0x0001);
    USB.manufacturerName("Smartx");
    USB.productName("X714");
    USB.usbAttributes(0x80);
    USB.begin();
    my_usb.begin(115200);
    delay(1000);

    WiFi.mode(WIFI_OFF);
    BLEDevice::deinit(true);

    serial_write("\nESP32 RF TEST FIRMWARE READY");
    serial_write("Type HELP for full documentation\n");
}

// ======================================================
// LOOP
// ======================================================
void loop()
{
    while (Serial.available())
    {
        char c = Serial.read();

        if (c == '\n')
        {
            handleCommand(inputBuffer);
            inputBuffer = "";
        }
        else
        {
            inputBuffer += c;
        }
    }

    while (my_usb.available())
    {
        char c = my_usb.read();

        if (c == '\n')
        {
            handleCommand(inputBuffer);
            inputBuffer = "";
        }
        else
        {
            inputBuffer += c;
        }
    }
}