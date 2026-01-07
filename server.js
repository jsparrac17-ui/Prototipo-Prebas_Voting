const express = require('express');
const http = require('http');
const { Server } = require("socket.io");
const { SerialPort } = require('serialport');
const { ReadlineParser } = require('@serialport/parser-readline');
const fs = require('fs');
const path = require('path');

// 1. Configuración Web
const app = express();
const server = http.createServer(app);
const io = new Server(server);

// Servir los archivos de la carpeta 'public' (donde estará el HTML)
app.use(express.static('public'));

// 2. Configuración de persistencia
const dataDir = path.join(__dirname, 'data');
const votesLogPath = path.join(dataDir, 'votes-log.jsonl');

if (!fs.existsSync(dataDir)) {
    fs.mkdirSync(dataDir, { recursive: true });
}

const resetVotesLog = () => {
    if (fs.existsSync(votesLogPath)) {
        fs.unlinkSync(votesLogPath);
    }
};

// Comenzar siempre con un log limpio para que los datos correspondan a la última votación
resetVotesLog();

// 3. Configuración USB (Gateway)
const portName = 'COM3'; // <--- ¡CAMBIA ESTO POR TU PUERTO!
const baudRate = 115200;

let arduinoPort;

try {
    arduinoPort = new SerialPort({ path: portName, baudRate: baudRate });
    const parser = arduinoPort.pipe(new ReadlineParser({ delimiter: '\r\n' }));

    console.log(`🔌 Conectando al Gateway en ${portName}...`);

    arduinoPort.on('error', (err) => {
        console.error(`❌ Error en puerto serie: ${err.message}`);
    });

    // Cuando llega un dato del ESP32
    parser.on('data', (data) => {
        try {
            // data llega como: {"id":101,"voto":1}
            const jsonVoto = JSON.parse(data);

            console.log("Voto recibido:", jsonVoto);

            // Persistir en archivo JSONL con marca de tiempo (solo la última votación)
            const enriched = { ...jsonVoto, receivedAt: new Date().toISOString() };
            fs.appendFile(votesLogPath, JSON.stringify(enriched) + "\n", (err) => {
                if (err) {
                    console.error('❌ Error guardando voto en archivo:', err.message);
                }
            });

            // ¡ENVIAR AL NAVEGADOR EN TIEMPO REAL!
            io.emit('nuevo-voto', jsonVoto);

        } catch (e) {
            console.error("Error parseando:", e.message);
        }
    });

} catch (error) {
    console.log("❌ No se pudo abrir el puerto serie. (¿Está conectado?)");
}

// 3.1 Endpoint para descargar el log de votos (solo la última votación) en CSV
app.get('/api/votes-log', (req, res) => {
    if (!fs.existsSync(votesLogPath)) {
        return res.status(404).json({ error: 'Aún no hay votos registrados' });
    }

    try {
        const raw = fs.readFileSync(votesLogPath, 'utf8');
        const lines = raw.split(/\r?\n/).filter(Boolean);
        const entries = lines.map((line) => JSON.parse(line));

        const header = ['id', 'voto', 'receivedAt'];
        const csvRows = [header.join(',')];
        entries.forEach((row) => {
            const values = header.map((key) => {
                const value = row[key] ?? '';
                // Escapar comas y comillas dobles
                if (typeof value === 'string' && /[",\n]/.test(value)) {
                    return `"${value.replace(/"/g, '""')}"`;
                }
                return value;
            });
            csvRows.push(values.join(','));
        });

        const csv = csvRows.join('\n');
        res.setHeader('Content-Type', 'text/csv');
        res.setHeader('Content-Disposition', 'attachment; filename="votes-log.csv"');
        res.send(csv);
    } catch (err) {
        console.error('❌ Error generando CSV del log:', err.message);
        res.status(500).json({ error: 'No se pudo generar el CSV' });
    }
});

// 3.1.1 Endpoint para reiniciar el log (limpiar historial)
app.post('/api/reset-log', (req, res) => {
    try {
        resetVotesLog();
        res.json({ ok: true });
    } catch (err) {
        console.error('❌ Error limpiando el log de votos:', err.message);
        res.status(500).json({ error: 'No se pudo limpiar el log' });
    }
});

// 3.2 Recepción de conexiones desde el navegador (solo monitoreo)
io.on('connection', (socket) => {
    console.log('🧭 Cliente conectado a Socket.IO');

    socket.on('start-flood', () => {
        if (arduinoPort) {
            arduinoPort.write('START\n');
            console.log('▶️  START enviado al receptor por Serial');
        } else {
            console.log('❌ No hay puerto serie abierto para enviar START');
        }
    });

    socket.on('stop-flood', () => {
        if (arduinoPort) {
            arduinoPort.write('STOP\n');
            console.log('⏹️  STOP enviado al receptor por Serial');
        } else {
            console.log('❌ No hay puerto serie abierto para enviar STOP');
        }
    });

    socket.on('disconnect', () => {
        console.log('👋 Cliente desconectado de Socket.IO');
    });
});

// 3. Iniciar Servidor
server.listen(3000, () => {
  console.log('---------------------------------------------------');
  console.log('✅ SISTEMA WISE LISTO');
  console.log('👉 Abre tu navegador en: http://localhost:3000');
  console.log('---------------------------------------------------');
});