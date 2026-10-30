#pragma once

#include <Arduino.h>

const char PAGE_INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>TCS34725 实时颜色示波器 (HSV / RGB 双模)</title>
  <style>
    :root {
      --bg: #0b0f19;
      --card-bg: rgba(22, 30, 49, 0.75);
      --border: rgba(255, 255, 255, 0.08);
      --text: #f1f5f9;
      --text-muted: #94a3b8;
      --color-h: #10b981;
      --color-s: #facc15; /* S 饱和度改为鲜明黄色 */
      --color-v: #c084fc;
      --color-r: #ef4444;
      --color-g: #22c55e;
      --color-b: #3b82f6;
      --color-c: #facc15;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      background: var(--bg);
      color: var(--text);
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
      padding: 16px;
      min-height: 100vh;
    }
    .header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding: 12px 20px;
      background: var(--card-bg);
      backdrop-filter: blur(12px);
      border: 1px solid var(--border);
      border-radius: 12px;
      margin-bottom: 16px;
    }
    .header-left { display: flex; align-items: center; gap: 12px; }
    .header h1 { font-size: 1.15rem; font-weight: 600; letter-spacing: 0.5px; }
    .status-badge {
      display: inline-flex;
      align-items: center;
      gap: 6px;
      font-size: 0.8rem;
      padding: 4px 10px;
      border-radius: 20px;
      background: rgba(16, 185, 129, 0.15);
      color: #34d399;
      border: 1px solid rgba(16, 185, 129, 0.3);
    }
    .status-dot { width: 8px; height: 8px; border-radius: 50%; background: #10b981; animation: pulse 2s infinite; }
    @keyframes pulse { 0%, 100% { opacity: 1; transform: scale(1); } 50% { opacity: 0.4; transform: scale(0.85); } }
    .header-actions { display: flex; gap: 10px; align-items: center; }
    .btn {
      background: #1e293b;
      color: var(--text);
      border: 1px solid var(--border);
      padding: 6px 14px;
      border-radius: 8px;
      font-size: 0.85rem;
      cursor: pointer;
      display: inline-flex;
      align-items: center;
      gap: 6px;
      transition: all 0.2s;
    }
    .btn:hover { background: #334155; }
    .btn.active { background: #0284c7; border-color: #38bdf8; }
    .btn-led-on { background: rgba(245, 158, 11, 0.2); border-color: #f59e0b; color: #fbbf24; }

    /* 模式切换分段按钮 */
    .view-switch {
      display: inline-flex;
      background: #0f172a;
      border: 1px solid var(--border);
      border-radius: 8px;
      padding: 3px;
      gap: 4px;
    }
    .view-btn {
      background: transparent;
      border: none;
      color: var(--text-muted);
      padding: 5px 12px;
      border-radius: 6px;
      font-size: 0.82rem;
      font-weight: 600;
      cursor: pointer;
      transition: all 0.2s;
    }
    .view-btn.active {
      background: #3b82f6;
      color: #ffffff;
      box-shadow: 0 2px 8px rgba(59, 130, 246, 0.4);
    }

    /* 数据卡片网格: 4 列布局，第一列为大面积色块卡片 */
    .cards-grid {
      display: grid;
      grid-template-columns: 240px 1fr 1.3fr 1.3fr;
      gap: 16px;
      margin-bottom: 16px;
    }
    @media (max-width: 1050px) { .cards-grid { grid-template-columns: 1fr 1fr; } }
    @media (max-width: 650px) { .cards-grid { grid-template-columns: 1fr; } }
    .card {
      background: var(--card-bg);
      border: 1px solid var(--border);
      border-radius: 12px;
      padding: 14px 18px;
      backdrop-filter: blur(10px);
      display: flex;
      flex-direction: column;
      justify-content: center;
    }
    .card-swatch {
      padding: 10px 14px;
      justify-content: space-between;
    }
    .card-swatch-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 8px;
    }
    .top-swatch-box {
      width: 100%;
      height: 48px;
      border-radius: 8px;
      border: 2px solid rgba(255, 255, 255, 0.25);
      box-shadow: inset 0 0 10px rgba(0, 0, 0, 0.5);
      background-color: #050403;
      transition: background-color 0.15s ease, box-shadow 0.2s ease;
    }
    .hud-hex {
      font-size: 0.88rem;
      font-family: ui-monospace, SFMono-Regular, monospace;
      font-weight: 700;
      color: #38bdf8;
      background: rgba(56, 189, 248, 0.1);
      padding: 2px 6px;
      border-radius: 4px;
      border: 1px solid rgba(56, 189, 248, 0.25);
    }
    .card-title { font-size: 0.75rem; text-transform: uppercase; color: var(--text-muted); font-weight: 600; margin-bottom: 6px; }
    .card-val { font-size: 1.4rem; font-weight: 700; font-family: ui-monospace, SFMono-Regular, monospace; }

    /* 示波器主图区 */
    .chart-container {
      background: var(--card-bg);
      border: 1px solid var(--border);
      border-radius: 12px;
      padding: 16px;
      backdrop-filter: blur(12px);
      position: relative;
    }
    .chart-toolbar {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 12px;
      flex-wrap: wrap;
      gap: 12px;
    }
    .chart-left-controls {
      display: flex;
      align-items: center;
      gap: 14px;
      flex-wrap: wrap;
    }
    .legend-group { display: flex; gap: 14px; align-items: center; font-size: 0.85rem; }
    .legend-item { display: flex; align-items: center; gap: 6px; cursor: pointer; user-select: none; }
    .legend-color { width: 12px; height: 12px; border-radius: 3px; }

    /* 图表内嵌颜色示例块 (Swatch Badge) */
    .chart-swatch-badge {
      display: inline-flex;
      align-items: center;
      gap: 8px;
      background: rgba(15, 23, 42, 0.8);
      border: 1px solid var(--border);
      padding: 4px 10px;
      border-radius: 20px;
      font-size: 0.8rem;
    }
    .swatch-circle {
      width: 18px;
      height: 18px;
      border-radius: 50%;
      border: 2px solid rgba(255, 255, 255, 0.3);
      box-shadow: 0 0 8px rgba(0, 0, 0, 0.5);
      background-color: #333;
      transition: background-color 0.15s;
    }

    .canvas-wrapper {
      position: relative;
      width: 100%;
      border-radius: 8px;
      overflow: hidden;
      border: 1px solid rgba(255, 255, 255, 0.05);
    }
    canvas {
      width: 100%;
      height: 440px;
      display: block;
      background: #070a12;
    }
  </style>
</head>
<body>

  <!-- 顶部导航与状态 -->
  <header class="header">
    <div class="header-left">
      <h1>TCS34725 颜色分析仪</h1>
      <span class="status-badge" id="wsStatus">
        <span class="status-dot"></span>
        <span id="wsStatusText">连接中...</span>
      </span>
    </div>
    <div class="header-actions">
      <div style="display: inline-flex; align-items: center; gap: 6px; font-size: 0.82rem; background: #1e293b; padding: 4px 8px; border-radius: 8px; border: 1px solid var(--border);">
        <span style="color: var(--text-muted);">灵敏度:</span>
        <select id="selGain" onchange="changeGain(this.value)" style="background: transparent; color: #38bdf8; border: none; font-weight: 700; cursor: pointer; outline: none;">
          <option value="1X" style="background:#1e293b;color:#fff;">1X (基准)</option>
          <option value="4X" style="background:#1e293b;color:#fff;">4X (标称)</option>
          <option value="16X" selected style="background:#1e293b;color:#fff;">16X (+4倍提升)</option>
          <option value="60X" style="background:#1e293b;color:#fff;">60X (极敏感)</option>
        </select>
      </div>
      <button class="btn btn-led-on" id="btnLed" onclick="toggleLed()">
        <span>💡 补光灯: ON</span>
      </button>
      <button class="btn" id="btnPause" onclick="togglePause()">
        <span>⏸ 暂停采集</span>
      </button>
      <button class="btn" onclick="clearData()">
        <span>🗑 清空历史</span>
      </button>
    </div>
  </header>

  <!-- 实时数据看板 (第一行，包含大样本色块与核心指标) -->
  <section class="cards-grid">
    <div class="card card-swatch">
      <div class="card-swatch-header">
        <span class="card-title" style="margin: 0;">实时探测色彩</span>
        <span class="hud-hex" id="topHex">#000000</span>
      </div>
      <div class="top-swatch-box" id="topSwatch"></div>
    </div>
    <div class="card">
      <div class="card-title">Clear 明度强度 (空载/物料基准)</div>
      <div class="card-val" id="valClear">---</div>
    </div>
    <div class="card">
      <div class="card-title">原始 RGB 通道 [R, G, B]</div>
      <div class="card-val" id="valRgb">[---, ---, ---]</div>
    </div>
    <div class="card">
      <div class="card-title">OpenCV 8-Bit HSV [H, S, V]</div>
      <div class="card-val" id="valHsv">[---, ---, ---]</div>
    </div>
  </section>

  <!-- 动态波形示波器 -->
  <section class="chart-container">
    <div class="chart-toolbar">
      <div class="chart-left-controls">
        <!-- 曲线模式切换器 -->
        <div class="view-switch">
          <button class="view-btn active" id="btnModeHsv" onclick="setViewMode('HSV')">HSV 空间</button>
          <button class="view-btn" id="btnModeRgb" onclick="setViewMode('RGB')">RGB 通道</button>
        </div>

        <!-- 工具栏实时色块指示 -->
        <div class="chart-swatch-badge">
          <div class="swatch-circle" id="toolbarSwatch"></div>
          <span>实时样本色彩</span>
        </div>

        <!-- 动态图例 (根据模式自适应) -->
        <div class="legend-group" id="legendGroup">
          <!-- 由 JS 动态渲染图例复选框 -->
        </div>
      </div>

      <div style="font-size: 0.8rem; color: var(--text-muted);">
        周期: <strong>200ms (5Hz)</strong> | 缓冲: <span id="sampleCount">0</span> 帧
      </div>
    </div>

    <!-- 示波器画布 (全画幅纯净显示，不遮挡曲线) -->
    <div class="canvas-wrapper">
      <canvas id="scopeCanvas"></canvas>
    </div>
  </section>

  <script>
    const MAX_POINTS = 150; // 显示最近 30 秒 (150点 * 200ms)
    const historyData = [];
    let isPaused = false;
    let ws = null;
    let currentLedState = 1;
    let currentMode = 'HSV'; // 'HSV' 或 'RGB'

    // 图例开关状态
    const traceVisibility = {
      h: true, s: true, v: true,
      r: true, g: true, b: true, c: false
    };

    // DOM 元素
    const canvas = document.getElementById('scopeCanvas');
    const ctx = canvas.getContext('2d');
    const valClear = document.getElementById('valClear');
    const valRgb = document.getElementById('valRgb');
    const valHsv = document.getElementById('valHsv');
    const wsStatusText = document.getElementById('wsStatusText');
    const btnLed = document.getElementById('btnLed');
    const btnPause = document.getElementById('btnPause');
    const sampleCountSpan = document.getElementById('sampleCount');
    const toolbarSwatch = document.getElementById('toolbarSwatch');
    const hudSwatch = document.getElementById('hudSwatch');
    const hudHex = document.getElementById('hudHex');
    const legendGroup = document.getElementById('legendGroup');
    const btnModeHsv = document.getElementById('btnModeHsv');
    const btnModeRgb = document.getElementById('btnModeRgb');

    function setViewMode(mode) {
      currentMode = mode;
      btnModeHsv.classList.toggle('active', mode === 'HSV');
      btnModeRgb.classList.toggle('active', mode === 'RGB');
      updateLegendUI();
      renderChart();
    }

    function updateLegendUI() {
      if (currentMode === 'HSV') {
        legendGroup.innerHTML = `
          <label class="legend-item" style="color: var(--color-h);">
            <input type="checkbox" ${traceVisibility.h ? 'checked' : ''} onchange="traceVisibility.h = this.checked; renderChart();">
            <span class="legend-color" style="background: var(--color-h);"></span>
            <strong>H 色相 [0~180]</strong>
          </label>
          <label class="legend-item" style="color: var(--color-s);">
            <input type="checkbox" ${traceVisibility.s ? 'checked' : ''} onchange="traceVisibility.s = this.checked; renderChart();">
            <span class="legend-color" style="background: var(--color-s);"></span>
            <strong>S 饱和度 [0~255]</strong>
          </label>
          <label class="legend-item" style="color: var(--color-v);">
            <input type="checkbox" ${traceVisibility.v ? 'checked' : ''} onchange="traceVisibility.v = this.checked; renderChart();">
            <span class="legend-color" style="background: var(--color-v);"></span>
            <strong>V 明度 [0~255]</strong>
          </label>
        `;
      } else {
        legendGroup.innerHTML = `
          <label class="legend-item" style="color: var(--color-r);">
            <input type="checkbox" ${traceVisibility.r ? 'checked' : ''} onchange="traceVisibility.r = this.checked; renderChart();">
            <span class="legend-color" style="background: var(--color-r);"></span>
            <strong>R 红通道</strong>
          </label>
          <label class="legend-item" style="color: var(--color-g);">
            <input type="checkbox" ${traceVisibility.g ? 'checked' : ''} onchange="traceVisibility.g = this.checked; renderChart();">
            <span class="legend-color" style="background: var(--color-g);"></span>
            <strong>G 绿通道</strong>
          </label>
          <label class="legend-item" style="color: var(--color-b);">
            <input type="checkbox" ${traceVisibility.b ? 'checked' : ''} onchange="traceVisibility.b = this.checked; renderChart();">
            <span class="legend-color" style="background: var(--color-b);"></span>
            <strong>B 蓝通道</strong>
          </label>
          <label class="legend-item" style="color: var(--color-c);">
            <input type="checkbox" ${traceVisibility.c ? 'checked' : ''} onchange="traceVisibility.c = this.checked; renderChart();">
            <span class="legend-color" style="background: var(--color-c);"></span>
            <strong>Clear 明度</strong>
          </label>
        `;
      }
    }

    // 画布高清适配
    function resizeCanvas() {
      const rect = canvas.getBoundingClientRect();
      const dpr = window.devicePixelRatio || 1;
      canvas.width = rect.width * dpr;
      canvas.height = rect.height * dpr;
      ctx.scale(dpr, dpr);
      renderChart();
    }
    window.addEventListener('resize', resizeCanvas);

    // WebSocket 连接
    function connectWS() {
      const host = window.location.hostname || '192.168.1.100';
      const wsUrl = `ws://${host}:81/`;
      ws = new WebSocket(wsUrl);

      ws.onopen = () => {
        wsStatusText.textContent = `已连接 (${host}:81)`;
      };

      ws.onmessage = (evt) => {
        if (isPaused) return;
        try {
          const d = JSON.parse(evt.data);
          updateData(d);
        } catch(e) { console.error("JSON parse err:", e); }
      };

      ws.onclose = () => {
        wsStatusText.textContent = '连接断开，3秒后重连...';
        setTimeout(connectWS, 3000);
      };
      ws.onerror = () => ws.close();
    }

    function updateData(d) {
      valClear.textContent = d.c;
      valRgb.textContent = `[${d.r}, ${d.g}, ${d.b}]`;
      valHsv.textContent = `[${d.h}, ${d.s}, ${d.v}]`;

      // 真实物理色彩映射：保持暗光下真实暗黑，强光下等比防溢出 (绝不人为放大微弱底噪)
      const rawMax = Math.max(d.r, d.g, d.b);
      const scale = rawMax > 255 ? (255.0 / rawMax) : 1.0;
      const pr = Math.min(255, Math.max(0, Math.round(d.r * scale)));
      const pg = Math.min(255, Math.max(0, Math.round(d.g * scale)));
      const pb = Math.min(255, Math.max(0, Math.round(d.b * scale)));
      const hex = `#${pr.toString(16).padStart(2,'0')}${pg.toString(16).padStart(2,'0')}${pb.toString(16).padStart(2,'0')}`;

      // 同步图表工具栏与顶部大卡片的颜色指示块
      toolbarSwatch.style.backgroundColor = hex;
      const topSwatch = document.getElementById('topSwatch');
      const topHex = document.getElementById('topHex');
      if (topSwatch) {
        topSwatch.style.backgroundColor = hex;
        topSwatch.style.boxShadow = `inset 0 0 10px rgba(0,0,0,0.5), 0 0 16px ${hex}77`;
      }
      if (topHex) topHex.textContent = hex.toUpperCase();

      // 同步 LED 状态
      if (d.led !== undefined && d.led !== currentLedState) {
        currentLedState = d.led;
        btnLed.className = currentLedState ? "btn btn-led-on" : "btn";
        btnLed.innerHTML = currentLedState ? "💡 补光灯: ON" : "💡 补光灯: OFF";
      }

      // 插入时域队列
      historyData.push({ r: d.r, g: d.g, b: d.b, c: d.c, h: d.h, s: d.s, v: d.v });
      if (historyData.length > MAX_POINTS) historyData.shift();
      sampleCountSpan.textContent = historyData.length;

      renderChart();
    }

    function renderChart() {
      const rect = canvas.getBoundingClientRect();
      const W = rect.width;
      const H = rect.height;
      if (!W || !H) return;

      ctx.clearRect(0, 0, W, H);

      const padL = 46, padR = 20, padT = 20, padB = 30;
      const plotW = W - padL - padR;
      const plotH = H - padT - padB;

      ctx.lineWidth = 1;
      ctx.strokeStyle = "rgba(255, 255, 255, 0.06)";
      ctx.fillStyle = "rgba(148, 163, 184, 0.6)";
      ctx.font = "10px ui-monospace, monospace";
      ctx.textAlign = "right";

      let yMax = 255;
      if (currentMode === 'RGB') {
        // RGB 模式下动态计算当前缓冲区内的最大值，保持波形饱满
        let localMax = 100;
        for (let i = 0; i < historyData.length; i++) {
          const pt = historyData[i];
          if (traceVisibility.r) localMax = Math.max(localMax, pt.r);
          if (traceVisibility.g) localMax = Math.max(localMax, pt.g);
          if (traceVisibility.b) localMax = Math.max(localMax, pt.b);
          if (traceVisibility.c) localMax = Math.max(localMax, pt.c);
        }
        yMax = Math.ceil(localMax * 1.15 / 50) * 50; // 取 50 整数倍余量
      }

      // 绘制 Y 轴等分参考线
      const steps = 5;
      for (let s = 0; s <= steps; s++) {
        const val = Math.round((yMax / steps) * s);
        const y = padT + plotH - (val / yMax) * plotH;
        ctx.beginPath();
        ctx.moveTo(padL, y);
        ctx.lineTo(padL + plotW, y);
        ctx.stroke();
        ctx.fillText(val, padL - 6, y + 3);
      }

      // HSV 模式下绘制 H=180 参考上限线
      if (currentMode === 'HSV') {
        const y180 = padT + plotH - (180 / 255) * plotH;
        ctx.save();
        ctx.setLineDash([4, 4]);
        ctx.strokeStyle = "rgba(16, 185, 129, 0.25)";
        ctx.beginPath();
        ctx.moveTo(padL, y180);
        ctx.lineTo(padL + plotW, y180);
        ctx.stroke();
        ctx.fillStyle = "rgba(16, 185, 129, 0.45)";
        ctx.textAlign = "left";
        ctx.fillText("H_Max (180)", padL + 8, y180 - 4);
        ctx.restore();
      }

      if (historyData.length < 2) return;

      function drawTrace(field, strokeColor, shadowColor) {
        ctx.save();
        ctx.beginPath();
        ctx.strokeStyle = strokeColor;
        ctx.lineWidth = 2.2;
        ctx.shadowColor = shadowColor;
        ctx.shadowBlur = 6;

        for (let i = 0; i < historyData.length; i++) {
          const val = historyData[i][field];
          const x = padL + (i / (MAX_POINTS - 1)) * plotW;
          const y = padT + plotH - (Math.min(val, yMax) / yMax) * plotH;
          if (i === 0) ctx.moveTo(x, y);
          else ctx.lineTo(x, y);
        }
        ctx.stroke();
        ctx.restore();
      }

      if (currentMode === 'HSV') {
        if (traceVisibility.v) drawTrace('v', '#c084fc', 'rgba(192, 132, 252, 0.4)');
        if (traceVisibility.s) drawTrace('s', '#facc15', 'rgba(250, 204, 21, 0.45)');
        if (traceVisibility.h) drawTrace('h', '#10b981', 'rgba(16, 185, 129, 0.4)');
      } else {
        if (traceVisibility.c) drawTrace('c', '#facc15', 'rgba(250, 204, 21, 0.4)');
        if (traceVisibility.b) drawTrace('b', '#3b82f6', 'rgba(59, 130, 246, 0.4)');
        if (traceVisibility.g) drawTrace('g', '#22c55e', 'rgba(34, 197, 94, 0.4)');
        if (traceVisibility.r) drawTrace('r', '#ef4444', 'rgba(239, 68, 68, 0.4)');
      }
    }

    function togglePause() {
      isPaused = !isPaused;
      btnPause.innerHTML = isPaused ? "▶ 继续采集" : "⏸ 暂停采集";
      btnPause.classList.toggle('active', isPaused);
    }

    function clearData() {
      historyData.length = 0;
      sampleCountSpan.textContent = "0";
      renderChart();
    }

    function toggleLed() {
      if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send("TOGGLE_LED");
      }
    }

    function changeGain(val) {
      if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send("SET_GAIN_" + val);
      }
    }

    // 初始化
    updateLegendUI();
    setTimeout(resizeCanvas, 50);
    connectWS();
  </script>
</body>
</html>
)rawliteral";
