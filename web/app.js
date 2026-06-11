const statusLabel = document.getElementById("status");
const refreshButton = document.getElementById("refresh");
const runButton = document.getElementById("runPrediction");
const eventInput = document.getElementById("eventKey");
const matchInput = document.getElementById("matchKey");
const topInput = document.getElementById("topCount");
const tableBody = document.querySelector("#statsTable tbody");
const errorPanel = document.getElementById("errorPanel");
const predictionPanel = document.getElementById("predictionPanel");

let chart;

async function fetchText(url) {
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`Request failed: ${response.status}`);
  }
  return response.text();
}

async function fetchJson(url) {
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`Request failed: ${response.status}`);
  }
  return response.json();
}

async function postJson(url, payload) {
  const response = await fetch(url, {
    method: "POST",
    headers: {
      "Content-Type": "application/json"
    },
    body: JSON.stringify(payload)
  });
  const body = await response.json();
  if (!response.ok) {
    const message = body.hint || body.stderr || body.error || `Request failed: ${response.status}`;
    throw new Error(message);
  }
  return body;
}

function parseCsv(csvText) {
  const lines = csvText.trim().split("\n");
  if (lines.length <= 1) {
    return [];
  }
  const rows = [];
  for (let i = 1; i < lines.length; i += 1) {
    const parts = lines[i].split(",");
    rows.push({
      rank: parts[0],
      team_key: parts[1],
      matches_played: parts[2],
      total_score: parts[3],
      average_score: parts[4],
      event_average_score: parts[5]
    });
  }
  return rows;
}

function formatPercent(value) {
  if (!Number.isFinite(Number(value))) {
    return "--";
  }
  return `${(Number(value) * 100).toFixed(1)}%`;
}

function formatNumber(value, digits = 1) {
  if (!Number.isFinite(Number(value))) {
    return "--";
  }
  return Number(value).toFixed(digits);
}

function showError(message) {
  errorPanel.textContent = message;
  errorPanel.hidden = false;
}

function clearError() {
  errorPanel.textContent = "";
  errorPanel.hidden = true;
}

function setBusy(isBusy) {
  runButton.disabled = isBusy;
  refreshButton.disabled = isBusy;
}

function renderTable(rows) {
  tableBody.innerHTML = "";
  rows.forEach((row) => {
    const tr = document.createElement("tr");
    tr.innerHTML = `
      <td>${row.rank}</td>
      <td>${row.team_key}</td>
      <td>${row.matches_played}</td>
      <td>${row.total_score}</td>
      <td>${formatNumber(row.average_score, 2)}</td>
      <td>${formatNumber(row.event_average_score, 2)}</td>
    `;
    tableBody.appendChild(tr);
  });
}

function renderChart(rows) {
  const labels = rows.map((row) => row.team_key);
  const data = rows.map((row) => Number(row.average_score));

  const ctx = document.getElementById("statsChart").getContext("2d");
  if (chart) {
    chart.destroy();
  }
  chart = new Chart(ctx, {
    type: "bar",
    data: {
      labels,
      datasets: [
        {
          label: "Average Score",
          data,
          backgroundColor: "rgba(37, 99, 235, 0.65)",
          borderColor: "rgba(30, 64, 175, 0.9)",
          borderWidth: 1
        }
      ]
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      plugins: {
        legend: {
          display: false
        }
      },
      scales: {
        y: {
          beginAtZero: true
        }
      }
    }
  });
}

function renderPrediction(prediction) {
  predictionPanel.hidden = false;
  document.getElementById("redWin").textContent = formatPercent(prediction.red_win_probability);
  document.getElementById("blueWin").textContent = formatPercent(prediction.blue_win_probability);
  document.getElementById("matchLabel").textContent = prediction.match_key || "--";
  document.getElementById("scoreDiff").textContent = formatNumber(prediction.adjusted_score_diff_estimate, 1);
  document.getElementById("redTeams").textContent = (prediction.red_teams || []).join(", ");
  document.getElementById("blueTeams").textContent = (prediction.blue_teams || []).join(", ");
  document.getElementById("redEstimate").textContent = formatNumber(prediction.red_score_total_estimate, 1);
  document.getElementById("blueEstimate").textContent = formatNumber(prediction.blue_score_total_estimate, 1);
  document.getElementById("redConfidence").textContent = formatPercent(prediction.red_confidence);
  document.getElementById("blueConfidence").textContent = formatPercent(prediction.blue_confidence);
}

function renderData(rows, prediction) {
  renderTable(rows);
  renderChart(rows.slice(0, 12));
  renderPrediction(prediction);
}

async function refreshFiles() {
  clearError();
  statusLabel.textContent = "Loading...";
  setBusy(true);
  try {
    const [statsCsv, prediction] = await Promise.all([
      fetchText("/api/stats"),
      fetchJson("/api/prediction")
    ]);

    renderData(parseCsv(statsCsv), prediction);
    statusLabel.textContent = "Updated from files";
  } catch (error) {
    showError(error.message);
    statusLabel.textContent = "Error";
  } finally {
    setBusy(false);
  }
}

async function runPrediction() {
  clearError();
  statusLabel.textContent = "Running...";
  setBusy(true);
  try {
    const result = await postJson("/api/prediction/run", {
      event_key: eventInput.value,
      match_key: matchInput.value,
      top: topInput.value
    });

    renderData(result.stats, result.prediction);
    statusLabel.textContent = `Updated ${new Date(result.generated_at).toLocaleTimeString()}`;
  } catch (error) {
    showError(error.message);
    statusLabel.textContent = "Error";
  } finally {
    setBusy(false);
  }
}

runButton.addEventListener("click", runPrediction);
refreshButton.addEventListener("click", refreshFiles);
eventInput.addEventListener("keydown", (event) => {
  if (event.key === "Enter") {
    runPrediction();
  }
});
matchInput.addEventListener("keydown", (event) => {
  if (event.key === "Enter") {
    runPrediction();
  }
});

refreshFiles();
