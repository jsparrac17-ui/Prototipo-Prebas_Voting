#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// Dirección broadcast para vinculación rápida (envía a cualquier receptor que escuche en el canal)
const uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Identificador único para este control (cámbialo en cada uno de los 5)
const uint8_t DEVICE_ID = 101; // 101,102,103,104,105

const uint8_t WIFI_CHANNEL = 6;
const uint16_t BURST_SIZE = 6;           // votos enviados en cada ráfaga
const uint16_t INTERVALO_MS = 40;        // tiempo entre ráfagas
const uint8_t BURSTS_ANTES_DE_PAUSA = 8; // tras este número de ráfagas hacemos una pausa extra
const uint16_t PAUSA_ESCUCHA_MS = 120;   // ventana para que entren comandos STOP en congestión

bool flooding = false;
unsigned long ultimaRafaga = 0;
uint16_t contadorBursts = 0;

// Estructura de los votos: 1=SI, 2=NO, 3=AB (abstención)
typedef struct __attribute__((packed)) {
  uint8_t controlId;
  uint8_t voto;
} VotoPacket;

// Comandos recibidos vía ESPNOW desde la app (enviados por el receptor)
typedef struct __attribute__((packed)) {
  uint8_t cmd; // 1 = START, 0 = STOP
} CommandPacket;

void onSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  if (status != ESP_NOW_SEND_SUCCESS) {
    Serial.printf("⚠️  Fallo al enviar paquete (status %d)\n", status);
  }
}

void onCommand(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
  (void)info;
  if (len != sizeof(CommandPacket)) return;

  CommandPacket cmd;
  memcpy(&cmd, incomingData, sizeof(CommandPacket));

  if (cmd.cmd == 1) {
    flooding = true;
    ultimaRafaga = 0;
    contadorBursts = 0;
    Serial.println("🚀 Ráfaga iniciada desde la app");
  } else if (cmd.cmd == 0) {
    flooding = false;
    contadorBursts = 0;
    Serial.println("⏹️  Ráfaga detenida desde la app");
  }
}

uint8_t votoAleatorio() {
  const uint8_t opciones[3] = {1, 2, 3};
  return opciones[random(0, 3)];
}

void enviarBurst() {
  VotoPacket pkt;
  pkt.controlId = DEVICE_ID;

  for (uint16_t i = 0; i < BURST_SIZE; i++) {
    pkt.voto = votoAleatorio();
    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *)&pkt, sizeof(pkt));
    if (result != ESP_OK) {
      Serial.printf("Error enviando voto %d: %d\n", i, result);
    }
    delay(2); // pequeño respiro para el radio
  }
}

void configESPNOW() {
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) {
    Serial.println("No se pudo iniciar ESPNOW");
    return;
  }

  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onCommand);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, broadcastAddress, 6);
  peer.channel = WIFI_CHANNEL;
  peer.encrypt = false;

  if (!esp_now_is_peer_exist(broadcastAddress)) {
    esp_err_t addStatus = esp_now_add_peer(&peer);
    if (addStatus != ESP_OK) {
      Serial.printf("No se pudo agregar peer (%d)\n", addStatus);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  randomSeed(esp_random());
  configESPNOW();
  Serial.printf("Control listo. MAC local: %s\n", WiFi.macAddress().c_str());
}

void loop() {
  if (flooding && (millis() - ultimaRafaga >= INTERVALO_MS)) {
    enviarBurst();
    ultimaRafaga = millis();

    // Tras varias ráfagas hacemos una pausa extra para escuchar STOP en canales saturados
    contadorBursts++;
    if (contadorBursts >= BURSTS_ANTES_DE_PAUSA) {
      delay(PAUSA_ESCUCHA_MS);
      contadorBursts = 0;
    }
  }
}
