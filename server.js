const express = require('express');
const http = require('http');
const { Server } = require("socket.io");
const { SerialPort } = require('serialport');
const { ReadlineParser } = require('@serialport/parser-readline');

// 1. Configuración Web
const app = express();
const server = http.createServer(app);
const io = new Server(server);

// Servir los archivos de la carpeta 'public' (donde estará el HTML)
app.use(express.static('public'));

// 2. Configuración USB (Gateway)
const portName = 'COM3'; // <--- ¡CAMBIA ESTO POR TU PUERTO!
const baudRate = 115200;

let arduinoPort;

try {
    arduinoPort = new SerialPort({ path: portName, baudRate: baudRate });
    const parser = arduinoPort.pipe(new ReadlineParser({ delimiter: '\r\n' }));

    console.log(`🔌 Conectando al Gateway en ${portName}...`);

    // Cuando llega un dato del ESP32
    parser.on('data', (data) => {
        try {
            // data llega como: {"id":101,"voto":1}
            const jsonVoto = JSON.parse(data);
            
            console.log("Voto recibido:", jsonVoto);
            
            // ¡ENVIAR AL NAVEGADOR EN TIEMPO REAL!
            io.emit('nuevo-voto', jsonVoto);
            
        } catch (e) {
            console.error("Error parseando:", e.message);
        }
    });

} catch (error) {
    console.log("❌ No se pudo abrir el puerto serie. (¿Está conectado?)");
}

// 3. Iniciar Servidor
server.listen(3000, () => {
  console.log('---------------------------------------------------');
  console.log('✅ SISTEMA WISE LISTO');
  console.log('👉 Abre tu navegador en: http://localhost:3000');
  console.log('---------------------------------------------------');
});