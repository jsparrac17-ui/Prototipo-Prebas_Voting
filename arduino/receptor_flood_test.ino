#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// Canales WiFi para ESPNOW (ajusta según tu entorno)
const uint8_t PRIMARY_CHANNEL = 6;
const uint8_t SECONDARY_CHANNEL = 1;
const bool DUAL_CHANNEL = true;

// Dirección broadcast para enviar comandos a todos los controles
const uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Estructura de los votos: 1=SI, 2=NO, 3=AB (abstención)
typedef struct __attribute__((packed)) {
  uint8_t controlId;
  uint8_t voto;
} VotoPacket;

typedef struct __attribute__((packed)) {
  uint8_t cmd; // 1 = START, 0 = STOP
} CommandPacket;

bool aceptarVotos = false;
bool stopBurstActivo = false;
unsigned long stopBurstHasta = 0;
unsigned long siguienteStopEnvio = 0;
bool receptorEnSilencio = false;
uint8_t canalActual = PRIMARY_CHANNEL;
unsigned long siguienteCambioCanal = 0;

const unsigned long STOP_BURST_MS = 900;
const unsigned long STOP_INTERVALO_MS = 60;
const unsigned long CHANNEL_SWITCH_MS = 120;

// Callback: se ejecuta cada vez que llega un paquete ESPNOW
// API actual: esp_now_register_recv_cb usa esp_now_recv_info_t como primer argumento
void onVote(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
  (void)info; // El MAC está en info->src_addr si lo necesitas
  if (!aceptarVotos) {
    return;
  }
  if (len != sizeof(VotoPacket)) {
    return;
  }

  VotoPacket voto;
  memcpy(&voto, incomingData, sizeof(VotoPacket));

  // Imprimir en formato JSON para el Node.js (server.js)
  Serial.print('{');
  Serial.print("\"id\":");
  Serial.print(voto.controlId);
  Serial.print(',');
  Serial.print("\"voto\":");
  Serial.print(voto.voto);
  Serial.println('}');
}

void configESPNOW() {
  // Fijar canal antes de inicializar ESPNOW
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(PRIMARY_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Fallo al iniciar ESPNOW");
    return;
  }

  esp_now_register_recv_cb(onVote);
  Serial.println("Receptor listo - esperando votos...");

  // Registrar peer broadcast para enviar comandos a los controles
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, broadcastAddress, 6);
  peer.channel = PRIMARY_CHANNEL;
  peer.encrypt = false;
  if (!esp_now_is_peer_exist(broadcastAddress)) {
    esp_now_add_peer(&peer);
  }
}

void fijarCanal(uint8_t canal) {
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(canal, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  canalActual = canal;
}

void enviarComando(uint8_t cmd) {
  CommandPacket packet{cmd};

  // Repetimos varias veces para que el STOP/START llegue incluso en congestión
  const uint8_t REPETICIONES = 8;
  for (uint8_t i = 0; i < REPETICIONES; i++) {
    esp_err_t status = esp_now_send(broadcastAddress, (uint8_t *)&packet, sizeof(packet));
    if (status != ESP_OK) {
      Serial.printf("Error enviando comando %u intento %u (%d)\n", cmd, i + 1, status);
    }
    delay(15); // pequeña ventana para despejar el aire
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  configESPNOW();
}

void loop() {
  // Escuchar comandos START/STOP provenientes del servidor por Serial
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toUpperCase();

    if (cmd == "START") {
      aceptarVotos = true;
      stopBurstActivo = false;
      fijarCanal(PRIMARY_CHANNEL);
      siguienteCambioCanal = millis() + CHANNEL_SWITCH_MS;
      if (receptorEnSilencio) {
        esp_now_register_recv_cb(onVote);
        receptorEnSilencio = false;
      }
      enviarComando(1);
      Serial.println("▶️  Comando START reenviado a controles (burst)");
    } else if (cmd == "STOP") {
      aceptarVotos = false;
      fijarCanal(PRIMARY_CHANNEL);
      if (!receptorEnSilencio) {
        esp_now_unregister_recv_cb();
        receptorEnSilencio = true;
      }
      stopBurstActivo = true;
      stopBurstHasta = millis() + STOP_BURST_MS;
      siguienteStopEnvio = 0;
      Serial.println("⏹️  Comando STOP en ráfagas extendidas");
    }
  }

  if (aceptarVotos && DUAL_CHANNEL && !receptorEnSilencio) {
    unsigned long ahora = millis();
    if (ahora >= siguienteCambioCanal) {
      uint8_t nuevoCanal = (canalActual == PRIMARY_CHANNEL) ? SECONDARY_CHANNEL : PRIMARY_CHANNEL;
      fijarCanal(nuevoCanal);
      siguienteCambioCanal = ahora + CHANNEL_SWITCH_MS;
    }
  }

  if (stopBurstActivo) {
    unsigned long ahora = millis();
    if (ahora >= stopBurstHasta) {
      stopBurstActivo = false;
    } else if (ahora >= siguienteStopEnvio) {
      enviarComando(0);
      siguienteStopEnvio = ahora + STOP_INTERVALO_MS;
    }
  }
}
