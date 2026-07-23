#include "SensorWebServer.h"
#include <WebServer.h>
#include "DHT11Sensor.h"
#include "WiFiManager.h"
#include "SharedState.h"
#include <time.h>

extern DHT11Sensor dht1;
extern DHT11Sensor dht2;
extern SensorHistory sensorHistory1;
extern SensorHistory sensorHistory2;
extern WiFiManager wifiManager;

// HTML 页面（带 Chart.js 折线图）
static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 传感器历史</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js"></script>
    <style>
        * { box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Arial, sans-serif;
            margin: 0; padding: 20px; background: #1a1a2e; color: #eee;
        }
        .container { max-width: 1200px; margin: 0 auto; }
        h1 { color: #00d2ff; text-align: center; margin-bottom: 30px; }
        .info {
            display: flex; justify-content: space-between; flex-wrap: wrap;
            background: #16213e; padding: 15px; border-radius: 10px; margin-bottom: 20px;
        }
        .info-item { margin: 5px 15px; }
        .info-label { color: #888; font-size: 12px; }
        .info-value { color: #00d2ff; font-size: 18px; font-weight: bold; }
        .card {
            background: #16213e; padding: 20px; border-radius: 10px; margin-bottom: 20px;
        }
        .card h2 { color: #00d2ff; margin-top: 0; }
        .chart-container { position: relative; height: 300px; }
        .current {
            display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 15px; margin-bottom: 20px;
        }
        .sensor {
            background: #16213e; padding: 15px; border-radius: 10px; text-align: center;
        }
        .sensor-label { color: #888; font-size: 12px; }
        .sensor-value { font-size: 28px; font-weight: bold; margin: 8px 0; }
        .sensor-unit { color: #888; font-size: 14px; }
        .t1 { color: #4dabf7; }
        .t2 { color: #ff9f43; }
        .h1 { color: #00d2ff; }
        .h2 { color: #51cf66; }
        .footer { text-align: center; color: #888; font-size: 12px; margin-top: 30px; }
    </style>
</head>
<body>
    <div class="container">
        <h1>📊 ESP32 传感器历史数据</h1>

        <div class="info">
            <div class="info-item">
                <div class="info-label">设备 IP</div>
                <div class="info-value" id="deviceIp">--</div>
            </div>
            <div class="info-item">
                <div class="info-label">WiFi 信号</div>
                <div class="info-value" id="rssi">-- dBm</div>
            </div>
            <div class="info-item">
                <div class="info-label">设备时间</div>
                <div class="info-value" id="deviceTime">--</div>
            </div>
            <div class="info-item">
                <div class="info-label">运行时长</div>
                <div class="info-value" id="uptime">--</div>
            </div>
        </div>

        <div class="current">
            <div class="sensor">
                <div class="sensor-label">温度 1</div>
                <div class="sensor-value t1" id="temp1">--</div>
                <div class="sensor-unit">°C</div>
            </div>
            <div class="sensor">
                <div class="sensor-label">湿度 1</div>
                <div class="sensor-value h1" id="hum1">--</div>
                <div class="sensor-unit">%</div>
            </div>
            <div class="sensor">
                <div class="sensor-label">温度 2</div>
                <div class="sensor-value t2" id="temp2">--</div>
                <div class="sensor-unit">°C</div>
            </div>
            <div class="sensor">
                <div class="sensor-label">湿度 2</div>
                <div class="sensor-value h2" id="hum2">--</div>
                <div class="sensor-unit">%</div>
            </div>
        </div>

        <div class="card">
            <h2>🌡️ 温度历史 (24小时)</h2>
            <div class="chart-container">
                <canvas id="tempChart"></canvas>
            </div>
        </div>

        <div class="card">
            <h2>💧 湿度历史 (24小时)</h2>
            <div class="chart-container">
                <canvas id="humChart"></canvas>
            </div>
        </div>

        <div class="footer">每 5 秒自动刷新 · 数据采集间隔 10 分钟 · 保留 7 天</div>
    </div>

    <script>
        // 通用图表配置
        const commonOptions = {
            responsive: true,
            maintainAspectRatio: false,
            interaction: { mode: 'index', intersect: false },
            plugins: {
                legend: { labels: { color: '#eee' } },
                tooltip: { backgroundColor: 'rgba(0,0,0,0.8)' }
            },
            scales: {
                x: {
                    ticks: { color: '#aaa', maxRotation: 0, autoSkipPadding: 20 },
                    grid: { color: 'rgba(255,255,255,0.05)' }
                },
                y: {
                    ticks: { color: '#aaa' },
                    grid: { color: 'rgba(255,255,255,0.1)' }
                }
            }
        };

        // 温度图表
        const tempCtx = document.getElementById('tempChart').getContext('2d');
        const tempChart = new Chart(tempCtx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [
                    {
                        label: '温度 1 (°C)',
                        borderColor: '#4dabf7',
                        backgroundColor: 'rgba(77,171,247,0.1)',
                        data: [],
                        tension: 0.3,
                        fill: true
                    },
                    {
                        label: '温度 2 (°C)',
                        borderColor: '#ff9f43',
                        backgroundColor: 'rgba(255,159,67,0.1)',
                        data: [],
                        tension: 0.3,
                        fill: true
                    }
                ]
            },
            options: { ...commonOptions, scales: { ...commonOptions.scales, y: { ...commonOptions.scales.y, suggestedMin: 0, suggestedMax: 50 } } }
        });

        // 湿度图表
        const humCtx = document.getElementById('humChart').getContext('2d');
        const humChart = new Chart(humCtx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [
                    {
                        label: '湿度 1 (%)',
                        borderColor: '#00d2ff',
                        backgroundColor: 'rgba(0,210,255,0.1)',
                        data: [],
                        tension: 0.3,
                        fill: true
                    },
                    {
                        label: '湿度 2 (%)',
                        borderColor: '#51cf66',
                        backgroundColor: 'rgba(81,207,102,0.1)',
                        data: [],
                        tension: 0.3,
                        fill: true
                    }
                ]
            },
            options: { ...commonOptions, scales: { ...commonOptions.scales, y: { ...commonOptions.scales.y, suggestedMin: 0, suggestedMax: 100 } } }
        });

        // 数据更新
        async function fetchData() {
            try {
                const r = await fetch('/api/current');
                const d = await r.json();
                document.getElementById('temp1').textContent = d.temp1.toFixed(1);
                document.getElementById('hum1').textContent = d.hum1.toFixed(1);
                document.getElementById('temp2').textContent = d.temp2.toFixed(1);
                document.getElementById('hum2').textContent = d.hum2.toFixed(1);
                document.getElementById('rssi').textContent = d.rssi + ' dBm';
                document.getElementById('deviceIp').textContent = d.ip;
                document.getElementById('deviceTime').textContent = d.time;
                document.getElementById('uptime').textContent = d.uptime;
            } catch (e) { console.error('fetch current', e); }
        }

        async function fetchHistory(type, chart) {
            try {
                const r = await fetch('/api/history?type=' + type);
                const d = await r.json();
                chart.data.labels = d.labels;
                chart.data.datasets[0].data = d.data1;
                chart.data.datasets[1].data = d.data2;
                chart.update('none');
            } catch (e) { console.error('fetch history', e); }
        }

        // 初次加载 + 定时刷新
        fetchData();
        fetchHistory('temp', tempChart);
        fetchHistory('hum', humChart);

        setInterval(fetchData, 5000);          // 当前数据每 5s
        setInterval(() => fetchHistory('temp', tempChart), 60000);  // 历史数据每 60s
        setInterval(() => fetchHistory('hum', humChart), 60000);
    </script>
</body>
</html>
)rawliteral";

SensorWebServer::SensorWebServer()
    : _server(nullptr), _running(false),
      _history1(nullptr), _history2(nullptr) {}

SensorWebServer::~SensorWebServer() {
    stop();
}

void SensorWebServer::begin() {
    _history1 = &sensorHistory1;
    _history2 = &sensorHistory2;
    Serial.println("[WebServer] 初始化完成");
}

void SensorWebServer::start() {
    if (_running) return;

    if (!_server) {
        _server = new WebServer(80);
        setupRoutes();
    }

    _server->begin();
    _running = true;
    Serial.println("[WebServer] HTTP 服务已启动");
    Serial.printf("[WebServer] 访问: http://%s/\n", wifiManager.getLocalIP().c_str());
}

void SensorWebServer::stop() {
    if (!_running && !_server) return;

    if (_server) {
        _server->stop();
        delete _server;
        _server = nullptr;
    }
    _running = false;
    Serial.println("[WebServer] HTTP 服务已停止");
}

void SensorWebServer::handleClient() {
    if (_running && _server) {
        _server->handleClient();
    }
}

void SensorWebServer::setupRoutes() {
    if (!_server) return;

    _server->on("/", HTTP_GET, [this]() { this->handleRoot(); });
    _server->on("/api/current", HTTP_GET, [this]() { this->handleApiData(); });
    _server->on("/api/history", HTTP_GET, [this]() { this->handleApiHistory(); });
    _server->onNotFound([this]() { this->handleNotFound(); });
}

void SensorWebServer::handleRoot() {
    _server->send(200, "text/html", INDEX_HTML);
}

void SensorWebServer::handleNotFound() {
    _server->send(404, "text/plain", "404: Not Found");
}

String SensorWebServer::buildCurrentJson() const {
    SensorSnapshot snap = getSensorSnapshot();
    float t1 = snap.t1, h1 = snap.h1, t2 = snap.t2, h2 = snap.h2;

    // 有效性检查：DHT11 温度 -20~60°C，湿度 1-100%RH
    static float prevT1 = -999, prevH1 = -999, prevT2 = -999, prevH2 = -999;
    if (!snap.t1Ok && prevT1 != -999) t1 = prevT1;
    if (!snap.h1Ok && prevH1 != -999) h1 = prevH1;
    if (!snap.t2Ok && prevT2 != -999) t2 = prevT2;
    if (!snap.h2Ok && prevH2 != -999) h2 = prevH2;
    if (snap.t1Ok) prevT1 = t1;
    if (snap.h1Ok) prevH1 = h1;
    if (snap.t2Ok) prevT2 = t2;
    if (snap.h2Ok) prevH2 = h2;

    String json = "{";
    json += "\"temp1\":" + String(t1, 1) + ",";
    json += "\"hum1\":" + String(h1, 1) + ",";
    json += "\"temp2\":" + String(t2, 1) + ",";
    json += "\"hum2\":" + String(h2, 1) + ",";
    json += "\"rssi\":" + String(wifiManager.getRSSI()) + ",";
    json += "\"ip\":\"" + wifiManager.getLocalIP() + "\",";

    struct tm timeinfo;
    char timeStr[16] = "--:--:--";
    if (getLocalTime(&timeinfo, 0)) {
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d",
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    }
    json += "\"time\":\"" + String(timeStr) + "\",";

    // 运行时间（毫秒 → 时分秒）
    unsigned long ms = millis();
    unsigned long secs = ms / 1000;
    unsigned long hours = secs / 3600;
    unsigned long mins = (secs % 3600) / 60;
    unsigned long seconds = secs % 60;
    char upStr[16];
    snprintf(upStr, sizeof(upStr), "%luh%lum", hours, mins);
    json += "\"uptime\":\"" + String(upStr) + "\"";
    json += "}";

    return json;
}

void SensorWebServer::handleApiData() {
    _server->send(200, "application/json", buildCurrentJson());
}

String SensorWebServer::buildHistoryJson(SensorHistory& history, const char* type) const {
    // 在 history mutex 下读取（TaskSample 写入时持有该锁）
    if (xSemaphoreTake(xHistoryMutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return "{\"error\":\"busy\"}";
    }

    int count = _history1->getCount();
    int count2 = _history2->getCount();
    int maxCount = count > count2 ? count : count2;

    String labels = "[";
    String data1 = "[";
    String data2 = "[";

    for (int i = 0; i < maxCount; i++) {
        // 标签：使用 history1 的时间
        if (i < count) {
            const SensorSample& s = _history1->getSample(i);
            char label[8];
            snprintf(label, sizeof(label), "%02d:%02d", s.hour, s.minute);
            if (i) labels += ",";
            labels += "\"" + String(label) + "\"";
        } else {
            if (i) labels += ",";
            labels += "\"\"";
        }

        // data1: history1 的温度或湿度
        if (i < count) {
            const SensorSample& s1 = _history1->getSample(i);
            float v = (strcmp(type, "temp") == 0) ? s1.temp : s1.humidity;
            if (i) data1 += ",";
            data1 += String(v, 1);
        } else {
            if (i) data1 += ",";
            data1 += "null";
        }

        // data2: history2 的温度或湿度
        if (i < count2) {
            const SensorSample& s2 = _history2->getSample(i);
            float v = (strcmp(type, "temp") == 0) ? s2.temp : s2.humidity;
            if (i) data2 += ",";
            data2 += String(v, 1);
        } else {
            if (i) data2 += ",";
            data2 += "null";
        }
    }

    labels += "]";
    data1 += "]";
    data2 += "]";

    xSemaphoreGive(xHistoryMutex);
    return "{\"labels\":" + labels + ",\"data1\":" + data1 + ",\"data2\":" + data2 + "}";
}

void SensorWebServer::handleApiHistory() {
    if (!_server->hasArg("type")) {
        _server->send(400, "application/json", "{\"error\":\"missing type\"}");
        return;
    }
    String type = _server->arg("type");
    if (type != "temp" && type != "hum") {
        _server->send(400, "application/json", "{\"error\":\"invalid type\"}");
        return;
    }

    String json = buildHistoryJson(*_history1, type.c_str());
    _server->send(200, "application/json", json);
}
