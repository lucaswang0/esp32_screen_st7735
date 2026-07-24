#include "SensorWebServer.h"
#include "Log.h"
#include <WebServer.h>
#include <FS.h>
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
        .chart-container { position: relative; height: 280px; }
        .date-picker { display: flex; align-items: center; gap: 10px; margin-bottom: 16px; }
        .date-picker label { color: #aaa; font-size: 14px; }
        .date-picker select {
            background: #1a2540; color: #fff; border: 1px solid rgba(255,255,255,0.15);
            border-radius: 6px; padding: 6px 12px; font-size: 14px; cursor: pointer;
        }
        .date-hint { color: #4dabf7; font-size: 13px; }
        .card h3 { color: #00d2ff; margin-top: 20px; margin-bottom: 10px; }
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
            <h2>📊 历史数据</h2>
            <div class="date-picker">
                <label>查看日期：</label>
                <select id="dateSelect"></select>
                <span id="dateHint" class="date-hint"></span>
            </div>

            <h3>🌡️ 温度</h3>
            <div class="chart-container">
                <canvas id="tempChart"></canvas>
            </div>

            <h3>💧 湿度</h3>
            <div class="chart-container">
                <canvas id="humChart"></canvas>
            </div>
        </div>

        <div class="footer">每 5 秒自动刷新 · 数据采集间隔 10 分钟 · 保留 7 天</div>
    </div>

    <script>
        // 当前选中的日期（YYYY-MM-DD），和每图缓存的 labels/timestamps
        let currentDate = null;
        const state = {
            temp: { labels: [], timestamps: [] },
            hum:  { labels: [], timestamps: [] }
        };

        // 通用图表配置
        const commonOptions = {
            responsive: true,
            maintainAspectRatio: false,
            interaction: { mode: 'nearest', intersect: false },
            plugins: {
                legend: { labels: { color: '#eee' } },
                tooltip: {
                    backgroundColor: 'rgba(0,0,0,0.8)',
                    callbacks: {
                        title: function(items) {
                            const item = items[0];
                            const chart = item.chart;
                            const target = chart.canvas.id === 'tempChart' ? 'temp' : 'hum';
                            const ts = item.parsed.x;
                            const idx = state[target].timestamps.indexOf(ts);
                            return idx >= 0 ? state[target].labels[idx] : '';
                        }
                    }
                }
            },
            scales: {
                x: {
                    type: 'linear',
                    min: 0,
                    max: 1440,
                    ticks: {
                        color: '#aaa',
                        maxRotation: 0,
                        autoSkip: true,
                        maxTicksLimit: 6,
                        stepSize: 240,
                        callback: function(value) {
                            // 找到最近的时间戳对应的 label
                            const chart = this.chart;
                            const target = chart.canvas.id === 'tempChart' ? 'temp' : 'hum';
                            const timestamps = state[target].timestamps;
                            const labels = state[target].labels;
                            const idx = timestamps.indexOf(value);
                            if (idx >= 0) return labels[idx];
                            // 兜底：按小时格式化
                            const h = Math.floor(value / 60);
                            const m = value % 60;
                            if (m === 0) return String(h).padStart(2, '0') + ':00';
                            return '';
                        }
                    },
                    grid: { color: 'rgba(255,255,255,0.05)' }
                },
                y: {
                    ticks: { color: '#aaa' },
                    grid: { color: 'rgba(255,255,255,0.1)' }
                }
            }
        };

        function makeDataset(label, borderColor, bgColor) {
            return {
                label: label,
                borderColor: borderColor,
                backgroundColor: bgColor,
                data: [],
                parsing: false,
                spanGaps: true,
                tension: 0.3,
                fill: true
            };
        }

        // 温度图表
        const tempCtx = document.getElementById('tempChart').getContext('2d');
        const tempChart = new Chart(tempCtx, {
            type: 'line',
            data: { datasets: [
                makeDataset('温度 1 (°C)', '#4dabf7', 'rgba(77,171,247,0.1)'),
                makeDataset('温度 2 (°C)', '#ff9f43', 'rgba(255,159,67,0.1)')
            ] },
            options: { ...commonOptions, scales: { ...commonOptions.scales, y: { ...commonOptions.scales.y, suggestedMin: 0, suggestedMax: 50 } } }
        });

        // 湿度图表
        const humCtx = document.getElementById('humChart').getContext('2d');
        const humChart = new Chart(humCtx, {
            type: 'line',
            data: { datasets: [
                makeDataset('湿度 1 (%)', '#00d2ff', 'rgba(0,210,255,0.1)'),
                makeDataset('湿度 2 (%)', '#51cf66', 'rgba(81,207,102,0.1)')
            ] },
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

        function setChartData(chart, d, target) {
            state[target].labels = d.labels || [];
            state[target].timestamps = d.timestamps || [];

            const toPoints = (arr) => {
                const out = [];
                for (let i = 0; i < arr.length; i++) {
                    if (arr[i] === null || arr[i] === undefined) continue;
                    out.push({ x: state[target].timestamps[i], y: arr[i] });
                }
                return out;
            };
            chart.data.datasets[0].data = toPoints(d.data1);
            chart.data.datasets[1].data = toPoints(d.data2);
            chart.update('none');
        }

        async function fetchHistory(target, chart) {
            if (!currentDate) return;
            try {
                const r = await fetch('/api/history?type=' + target + '&date=' + currentDate);
                const d = await r.json();
                if (d.error) { console.error('history error', d.error); return; }
                setChartData(chart, d, target);
            } catch (e) { console.error('fetch history', e); }
        }

        // 加载日期列表 + 初始化选择器
        async function loadDates() {
            try {
                const r = await fetch('/api/dates');
                const d = await r.json();
                const dates = d.dates || [];
                const today = d.today;

                const select = document.getElementById('dateSelect');
                select.innerHTML = '';

                if (dates.length === 0) {
                    const opt = document.createElement('option');
                    opt.textContent = '暂无数据';
                    opt.disabled = true;
                    select.appendChild(opt);
                    document.getElementById('dateHint').textContent = '请等待第一次采样';
                    return;
                }

                // 倒序（最新在前）
                dates.sort().reverse();
                for (const date of dates) {
                    const opt = document.createElement('option');
                    opt.value = date;
                    const md = date.slice(5);  // "07-22"
                    if (date === today) {
                        opt.textContent = '今天 (' + md + ')';
                    } else {
                        opt.textContent = md;
                    }
                    select.appendChild(opt);
                }

                // 默认选今天（如果今天没数据，选最近一天）
                if (dates.includes(today)) {
                    currentDate = today;
                } else {
                    currentDate = dates[0];
                }
                select.value = currentDate;

                const hint = document.getElementById('dateHint');
                if (currentDate === today) {
                    hint.textContent = '显示今日数据';
                } else {
                    hint.textContent = '今天暂无数据，显示最近一天';
                }
            } catch (e) {
                console.error('loadDates', e);
            }
        }

        document.getElementById('dateSelect').addEventListener('change', (e) => {
            currentDate = e.target.value;
            document.getElementById('dateHint').textContent = '显示 ' + currentDate + ' 数据';
            fetchHistory('temp', tempChart);
            fetchHistory('hum', humChart);
        });

        // 初次加载
        (async () => {
            await loadDates();
            fetchData();
            if (currentDate) {
                fetchHistory('temp', tempChart);
                fetchHistory('hum', humChart);
            }
        })();

        // 定时刷新（当前数据 5s，历史数据 60s）
        setInterval(fetchData, 5000);
        setInterval(() => {
            if (currentDate) {
                fetchHistory('temp', tempChart);
                fetchHistory('hum', humChart);
            }
        }, 60000);

        // 每分钟检查一次"今天"是否出现（跨天场景）
        setInterval(async () => {
            const r = await fetch('/api/dates');
            const d = await r.json();
            if (d.today && d.dates && d.dates.includes(d.today)) {
                const select = document.getElementById('dateSelect');
                if (![...select.options].some(o => o.value === d.today)) {
                    const opt = document.createElement('option');
                    opt.value = d.today;
                    opt.textContent = '今天 (' + d.today.slice(5) + ')';
                    select.insertBefore(opt, select.firstChild);
                }
            }
        }, 60000);
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
    LOG_LN("[WebServer] 初始化完成");
}

void SensorWebServer::start() {
    if (_running) return;

    if (!_server) {
        _server = new WebServer(80);
        setupRoutes();
    }

    _server->begin();
    _running = true;
    LOG_LN("[WebServer] HTTP 服务已启动");
    LOG_T("[WebServer] 访问: http://%s/", wifiManager.getLocalIP().c_str());
}

void SensorWebServer::stop() {
    if (!_running && !_server) return;

    if (_server) {
        _server->stop();
        delete _server;
        _server = nullptr;
    }
    _running = false;
    LOG_LN("[WebServer] HTTP 服务已停止");
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
    _server->on("/api/dates", HTTP_GET, [this]() { this->handleApiDates(); });
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

String SensorWebServer::buildHistoryJson(SensorHistory& history, const char* type, const char* date) const {
    if (xSemaphoreTake(xHistoryMutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return "{\"error\":\"busy\"}";
    }

    // 读取指定日期的两个传感器数据（不修改内部状态）
    static SensorSample buf1[MAX_SAMPLES];
    static SensorSample buf2[MAX_SAMPLES];
    int n1 = _history1->readByDate(date, buf1, MAX_SAMPLES);
    int n2 = _history2->readByDate(date, buf2, MAX_SAMPLES);
    int maxN = n1 > n2 ? n1 : n2;

    String labels, timestamps, data1, data2;
    labels.reserve(maxN * 8);
    timestamps.reserve(maxN * 6);
    data1.reserve(maxN * 6);
    data2.reserve(maxN * 6);

    labels = "[";
    timestamps = "[";
    data1 = "[";
    data2 = "[";

    for (int i = 0; i < maxN; i++) {
        if (i) {
            labels += ",";
            timestamps += ",";
            data1 += ",";
            data2 += ",";
        }
        // 优先用 buf1 的时间（两个传感器同时采样，时间一致）
        const SensorSample* s;
        if (i < n1) s = &buf1[i];
        else        s = &buf2[i];

        char labelBuf[8];
        snprintf(labelBuf, sizeof(labelBuf), "\"%02d:%02d\"", s->hour, s->minute);
        labels += labelBuf;
        timestamps += String(s->hour * 60 + s->minute);

        if (i < n1) {
            float v = (strcmp(type, "temp") == 0) ? buf1[i].temp : buf1[i].humidity;
            data1 += String(v, 1);
        } else {
            data1 += "null";
        }

        if (i < n2) {
            float v = (strcmp(type, "temp") == 0) ? buf2[i].temp : buf2[i].humidity;
            data2 += String(v, 1);
        } else {
            data2 += "null";
        }
    }
    labels += "]";
    timestamps += "]";
    data1 += "]";
    data2 += "]";

    xSemaphoreGive(xHistoryMutex);
    return "{\"date\":\"" + String(date ? date : "") + "\",\"labels\":" + labels +
           ",\"timestamps\":" + timestamps +
           ",\"data1\":" + data1 + ",\"data2\":" + data2 + "}";
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

    // date 参数：可选，缺省/空/"today" = 今天
    const char* date = nullptr;
    String dateArg;
    if (_server->hasArg("date")) {
        dateArg = _server->arg("date");
        if (dateArg.length() > 0 && dateArg != "today") {
            date = dateArg.c_str();
        }
    }

    String json = buildHistoryJson(*_history1, type.c_str(), date);
    _server->send(200, "application/json", json);
}

void SensorWebServer::handleApiDates() {
    // 获取今天的日期（设备本地时间）
    time_t now = time(NULL);
    struct tm* tm = localtime(&now);
    char today[16];
    snprintf(today, sizeof(today), "%04d-%02d-%02d",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);

    // 扫描 SPIFFS 中以 "/sensor1_" 开头、".dat" 结尾的文件，提取日期
    // （sensor1 与 sensor2 应同时存在，仅取一份即可）
    String json = "{\"today\":\"";
    json += today;
    json += "\",\"dates\":[";
    bool first = true;

    fs::File root = SPIFFS.open("/");
    if (root) {
        fs::File file = root.openNextFile();
        while (file) {
            String name = file.name();
            // SPIFFS 迭代时 file.name() 可能返回 "sensor1_2026-07-23.dat"
            // 也可能返回 "/sensor1_2026-07-23.dat"，统一去掉前导 /
            if (name.startsWith("/")) name = name.substring(1);
            if (name.startsWith("sensor1_") && name.endsWith(".dat")) {
                int dotIdx = name.lastIndexOf('.');
                if (dotIdx > 8) {  // "sensor1_" 长度 = 8
                    String date = name.substring(8, dotIdx);
                    if (date.length() == 10) {  // YYYY-MM-DD
                        if (!first) json += ",";
                        json += "\"" + date + "\"";
                        first = false;
                    }
                }
            }
            file = root.openNextFile();
        }
    }
    json += "]}";
    _server->send(200, "application/json", json);
}
