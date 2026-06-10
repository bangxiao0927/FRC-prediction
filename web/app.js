const statusLabel = document.getElementById("status");
const predictionBox = document.getElementById("prediction");
const refreshButton = document.getElementById("refresh");
const tableBody = document.querySelector("#statsTable tbody");
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
      team: parts[1],
      matches: parts[2],
      total: parts[3],
      average: parts[4],
      eventAverage: parts[5]
    });
  }
  return rows;
}

function renderTable(rows) {
  tableBody.innerHTML = "";
  rows.forEach((row) => {
    const tr = document.createElement("tr");
    tr.innerHTML = `
      <td>${row.rank}</td>
      <td>${row.team}</td>
      <td>${row.matches}</td>
      <td>${row.total}</td>
      <td>${row.average}</td>
      <td>${row.eventAverage}</td>
    `;
    tableBody.appendChild(tr);
  });
}

function renderChart(rows) {
  const labels = rows.map((row) => row.team);
  const data = rows.map((row) => Number(row.average));

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
          backgroundColor: "rgba(37, 99, 235, 0.6)"
        }
      ]
    },
    options: {
      responsive: true,
      plugins: {
        legend: {
          display: true
        }
      }
    }
  });
}

async function refresh() {
  statusLabel.textContent = "Loading...";
  try {
    const [statsCsv, prediction] = await Promise.all([
      fetchText("/api/stats"),
      fetchJson("/api/prediction")
    ]);

    const rows = parseCsv(statsCsv);
    renderTable(rows);
    renderChart(rows.slice(0, 12));
    predictionBox.textContent = JSON.stringify(prediction, null, 2);
    statusLabel.textContent = "Updated";
  } catch (error) {
    statusLabel.textContent = error.message;
  }
}

refreshButton.addEventListener("click", refresh);
refresh();
