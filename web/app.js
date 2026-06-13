const statusLabel = document.getElementById("status");
const refreshButton = document.getElementById("refresh");
const runButton = document.getElementById("runPrediction");
const eventInput = document.getElementById("eventKey");
const matchInput = document.getElementById("matchKey");
const topInput = document.getElementById("topCount");
const tableBody = document.querySelector("#statsTable tbody");
const errorPanel = document.getElementById("errorPanel");
const predictionPanel = document.getElementById("predictionPanel");
const autoRefreshToggle = document.getElementById("autoRefresh");
const autoIntervalSelect = document.getElementById("autoInterval");
const matchTeamsLabel = document.getElementById("matchTeams");
const picklistStrategy = document.getElementById("picklistStrategy");
const picklistExclude = document.getElementById("picklistExclude");
const picklistTop = document.getElementById("picklistTop");
const picklistTeam = document.getElementById("picklistTeam");
const buildPicklistButton = document.getElementById("buildPicklist");
const picklistStatus = document.getElementById("picklistStatus");
const picklistTableBody = document.querySelector("#picklistTable tbody");
const picklistSelfLabel = document.getElementById("picklistSelf");
const useHistoryToggle = document.getElementById("useHistory");
const historyTeamsInput = document.getElementById("historyTeams");
const historyBadge = document.getElementById("historyBadge");
const rolesPhase = document.getElementById("rolesPhase");
const rolesTop = document.getElementById("rolesTop");
const buildRolesButton = document.getElementById("buildRoles");
const rolesStatus = document.getElementById("rolesStatus");
const rolesNote = document.getElementById("rolesNote");
const rolesTableBody = document.querySelector("#rolesTable tbody");
const allianceTeams = document.getElementById("allianceTeams");
const allianceVs = document.getElementById("allianceVs");
const evalAllianceButton = document.getElementById("evalAlliance");
const allianceStatus = document.getElementById("allianceStatus");
const allianceResult = document.getElementById("allianceResult");

let chart;
let picklistChart;
let autoTimer = null;
let isBusy = false;

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

function setBusy(busy) {
  isBusy = busy;
  runButton.disabled = busy;
  refreshButton.disabled = busy;
}

function allianceColorOf(prediction, teamKey) {
  if (!prediction) {
    return null;
  }
  if ((prediction.red_teams || []).includes(teamKey)) {
    return "red";
  }
  if ((prediction.blue_teams || []).includes(teamKey)) {
    return "blue";
  }
  return null;
}

function teamAverage(teamKey, rowsMap, matchTeamStats) {
  if (rowsMap.has(teamKey)) {
    return Number(rowsMap.get(teamKey));
  }
  if (matchTeamStats && matchTeamStats[teamKey] !== undefined) {
    return Number(matchTeamStats[teamKey].average_score);
  }
  return null;
}

function renderMatchTeams(prediction, rows, matchTeamStats) {
  const red = (prediction && prediction.red_teams) || [];
  const blue = (prediction && prediction.blue_teams) || [];
  if (red.length === 0 && blue.length === 0) {
    matchTeamsLabel.hidden = true;
    return;
  }

  const rowsMap = new Map(rows.map((row) => [row.team_key, row.average_score]));
  const label = (team) => {
    const avg = teamAverage(team, rowsMap, matchTeamStats);
    return avg === null ? team : `${team} (${formatNumber(avg, 1)})`;
  };

  matchTeamsLabel.innerHTML =
    `<strong>${prediction.match_key || "match"}</strong>` +
    `<span class="chip chip-red">Red: ${red.map(label).join(", ")}</span>` +
    `<span class="chip chip-blue">Blue: ${blue.map(label).join(", ")}</span>`;
  matchTeamsLabel.hidden = false;
}

function renderTable(rows, prediction) {
  tableBody.innerHTML = "";
  rows.forEach((row) => {
    const tr = document.createElement("tr");
    const side = allianceColorOf(prediction, row.team_key);
    if (side) {
      tr.classList.add(side === "red" ? "row-red" : "row-blue");
    }
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

function barColor(side) {
  if (side === "red") {
    return "rgba(220, 38, 38, 0.8)";
  }
  if (side === "blue") {
    return "rgba(37, 99, 235, 0.8)";
  }
  return "rgba(148, 163, 184, 0.55)";
}

function renderChart(rows, prediction, matchTeamStats) {
  if (typeof Chart === "undefined") {
    // Chart.js (loaded from CDN) is unavailable; keep the table usable.
    return;
  }
  // Show the top teams by average, but always include the selected match's
  // teams even if they rank lower, so the highlight is meaningful.
  const rowsMap = new Map(rows.map((row) => [row.team_key, row.average_score]));
  const items = [];
  const seen = new Set();
  rows.slice(0, 12).forEach((row) => {
    items.push({ team: row.team_key, avg: Number(row.average_score) });
    seen.add(row.team_key);
  });
  const matchTeams = [
    ...((prediction && prediction.red_teams) || []),
    ...((prediction && prediction.blue_teams) || [])
  ];
  matchTeams.forEach((team) => {
    if (seen.has(team)) {
      return;
    }
    const avg = teamAverage(team, rowsMap, matchTeamStats);
    if (avg === null || Number.isNaN(avg)) {
      return;
    }
    items.push({ team, avg });
    seen.add(team);
  });
  items.sort((left, right) => right.avg - left.avg);

  const labels = items.map((item) => item.team);
  const data = items.map((item) => item.avg);
  const colors = items.map((item) => barColor(allianceColorOf(prediction, item.team)));

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
          backgroundColor: colors,
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
  const redWin = Number(prediction.red_win_probability) || 0;
  const blueWin = Number(prediction.blue_win_probability) || 0;

  // Win-probability bar. Normalize in case the two values don't sum to exactly 1.
  const total = redWin + blueWin || 1;
  const redPct = (redWin / total) * 100;
  document.getElementById("winbarRed").style.width = `${redPct}%`;
  document.getElementById("winbarBlue").style.width = `${100 - redPct}%`;
  document.getElementById("redWin").textContent = formatPercent(redWin);
  document.getElementById("blueWin").textContent = formatPercent(blueWin);
  document.getElementById("matchLabel").textContent = prediction.match_key || "--";

  // History badge: show whether the cross-event history blend was applied and to
  // which robots (empty list = all match teams).
  if (prediction.model_uses_history) {
    const teams = prediction.history_teams || [];
    historyBadge.textContent =
      teams.length > 0 ? `history: ${teams.join(", ")}` : "history: all teams";
    historyBadge.hidden = false;
  } else {
    historyBadge.hidden = true;
  }

  // Favored side + predicted margin.
  const margin = Number(prediction.adjusted_score_diff_estimate) || 0;
  const redFavored = margin >= 0;
  const favored = redFavored ? "Red" : "Blue";
  const favoredPct = formatPercent(redFavored ? redWin : blueWin);
  const callout = document.getElementById("winnerCallout");
  callout.textContent =
    `${favored} favored · ${favoredPct} · margin ${formatNumber(Math.abs(margin), 1)} pts`;
  callout.className = `winner-callout ${redFavored ? "red" : "blue"}`;

  // Alliance rosters.
  document.getElementById("redTeams").textContent = (prediction.red_teams || []).join(", ");
  document.getElementById("blueTeams").textContent = (prediction.blue_teams || []).join(", ");

  // Side-by-side metrics.
  document.getElementById("redEstimate").textContent = formatNumber(prediction.red_score_total_estimate, 1);
  document.getElementById("blueEstimate").textContent = formatNumber(prediction.blue_score_total_estimate, 1);
  document.getElementById("redWinMetric").textContent = formatPercent(redWin);
  document.getElementById("blueWinMetric").textContent = formatPercent(blueWin);
  document.getElementById("redConfidence").textContent = formatPercent(prediction.red_confidence);
  document.getElementById("blueConfidence").textContent = formatPercent(prediction.blue_confidence);
  document.getElementById("redMatches").textContent = formatNumber(prediction.red_average_matches, 1);
  document.getElementById("blueMatches").textContent = formatNumber(prediction.blue_average_matches, 1);
}

function renderData(rows, prediction, matchTeamStats) {
  renderMatchTeams(prediction, rows, matchTeamStats);
  renderTable(rows, prediction);
  renderChart(rows, prediction, matchTeamStats);
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

    renderData(parseCsv(statsCsv), prediction, {});
    statusLabel.textContent = "Updated from files";
  } catch (error) {
    // First run before any data exists: guide the user instead of erroring.
    if (String(error.message).includes("404")) {
      statusLabel.textContent = "Enter an event and press Run to begin";
    } else {
      showError(error.message);
      statusLabel.textContent = "Error";
    }
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
      top: topInput.value,
      use_history: useHistoryToggle.checked,
      history_teams: historyTeamsInput.value
    });

    renderData(result.stats, result.prediction, result.match_team_stats || {});
    const stamp = new Date(result.generated_at).toLocaleTimeString();
    const autoSuffix = autoRefreshToggle.checked
      ? ` · auto every ${autoIntervalSelect.value}s`
      : "";
    statusLabel.textContent = `Updated ${stamp}${autoSuffix}`;
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

function stopAutoRefresh() {
  if (autoTimer !== null) {
    clearInterval(autoTimer);
    autoTimer = null;
  }
}

function startAutoRefresh() {
  stopAutoRefresh();
  const seconds = Number(autoIntervalSelect.value) || 30;
  autoTimer = setInterval(() => {
    // Skip this tick if a run/refresh is still in flight to avoid overlap.
    if (isBusy) {
      return;
    }
    runPrediction();
  }, seconds * 1000);
}

function syncAutoRefresh() {
  if (autoRefreshToggle.checked) {
    startAutoRefresh();
  } else {
    stopAutoRefresh();
  }
}

autoRefreshToggle.addEventListener("change", syncAutoRefresh);
autoIntervalSelect.addEventListener("change", () => {
  // Restart the timer with the new interval only when auto-refresh is on.
  if (autoRefreshToggle.checked) {
    startAutoRefresh();
  }
});

function renderPicklistSelf(result) {
  const selfTeam = result.self_team_key;
  if (!selfTeam) {
    picklistSelfLabel.hidden = true;
    return;
  }
  // A prominent "pad" so the requesting team's status reads clearly and it is
  // obvious the candidate list below is scored relative to (and excludes) it.
  const tile = (label, value) =>
    `<div class="self-tile"><span class="self-tile-value">${value}</span>` +
    `<span class="self-tile-label">${label}</span></div>`;
  picklistSelfLabel.innerHTML =
    `<div class="self-pad-head">` +
    `<div><span class="self-pad-eyebrow">Your Team</span>` +
    `<span class="self-pad-team">${selfTeam}</span></div>` +
    `<span class="self-pad-tag">Excluded from picks</span></div>` +
    `<div class="self-tiles">` +
    tile("Avg Score", formatNumber(result.self_average_score, 1)) +
    tile("Recent Avg", formatNumber(result.self_recent_average, 1)) +
    tile("Std Dev", formatNumber(result.self_stddev, 1)) +
    tile("Matches", result.self_matches ?? "--") +
    tile("Event Avg", formatNumber(result.event_average_score, 1)) +
    `</div>`;
  picklistSelfLabel.hidden = false;
}

function renderPicklistTable(teams) {
  picklistTableBody.innerHTML = "";
  teams.forEach((team) => {
    const tr = document.createElement("tr");
    tr.innerHTML = `
      <td>${team.rank}</td>
      <td>${team.team_key}</td>
      <td>${formatNumber(team.picklist_score, 3)}</td>
      <td>${formatNumber(team.average_score, 1)}</td>
      <td>${formatNumber(team.stddev, 1)}</td>
      <td>${formatNumber(team.trend, 1)}</td>
      <td>${team.matches}</td>
    `;
    picklistTableBody.appendChild(tr);
  });
}

function renderPicklistChart(teams) {
  if (typeof Chart === "undefined") {
    return;
  }
  const top = teams.slice(0, 15);
  const labels = top.map((team) => team.team_key);
  const data = top.map((team) => Number(team.picklist_score));

  const ctx = document.getElementById("picklistChart").getContext("2d");
  if (picklistChart) {
    picklistChart.destroy();
  }
  picklistChart = new Chart(ctx, {
    type: "bar",
    data: {
      labels,
      datasets: [
        {
          label: "Picklist score",
          data,
          backgroundColor: "rgba(16, 185, 129, 0.75)",
          borderWidth: 1
        }
      ]
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      plugins: { legend: { display: false } },
      scales: { y: { beginAtZero: true } }
    }
  });
}

async function buildPicklist() {
  clearError();
  picklistStatus.textContent = "Building...";
  buildPicklistButton.disabled = true;
  try {
    const result = await postJson("/api/picklist/run", {
      event_key: eventInput.value,
      team_key: picklistTeam.value,
      strategy: picklistStrategy.value,
      exclude: picklistExclude.value,
      before: matchInput.value,
      top: picklistTop.value
    });

    const teams = result.teams || [];
    renderPicklistSelf(result);
    renderPicklistTable(teams);
    renderPicklistChart(teams);
    picklistStatus.textContent = `${teams.length} teams · ${result.strategy}`;
  } catch (error) {
    showError(error.message);
    picklistStatus.textContent = "Error";
  } finally {
    buildPicklistButton.disabled = false;
  }
}

buildPicklistButton.addEventListener("click", buildPicklist);

// ---- Team roles ----

function roleBadgeClass(role) {
  return `role-badge role-${role || "offense"}`;
}

function renderRolesTable(roles) {
  rolesTableBody.innerHTML = "";
  roles.forEach((role) => {
    const tr = document.createElement("tr");
    tr.innerHTML = `
      <td>${role.team_key}</td>
      <td><span class="${roleBadgeClass(role.primary_role)}">${role.primary_role}</span></td>
      <td>${formatNumber(role.offense, 1)}</td>
      <td>${formatNumber(role.auto, 1)}</td>
      <td>${formatNumber(role.teleop, 1)}</td>
      <td>${formatNumber(role.endgame, 1)}</td>
      <td>${formatNumber(role.defense, 1)}</td>
    `;
    rolesTableBody.appendChild(tr);
  });
}

function renderRolesNote(roles) {
  const first = roles[0];
  if (!first) {
    rolesNote.hidden = true;
    return;
  }
  let note = "";
  if (!first.has_phase_data) {
    note = "No score_breakdown for this event; phase ratings are 0.";
  } else if (first.has_endgame_data === false) {
    note = "This season's endgame breakdown is unknown; endgame is 0 and teleop still includes endgame points.";
  }
  rolesNote.textContent = note;
  rolesNote.hidden = note === "";
}

async function buildRoles() {
  clearError();
  rolesStatus.textContent = "Computing...";
  buildRolesButton.disabled = true;
  try {
    const result = await postJson("/api/roles/run", {
      event_key: eventInput.value,
      before: matchInput.value,
      phase: rolesPhase.value,
      top: rolesTop.value
    });
    const roles = result.roles || [];
    renderRolesTable(roles);
    renderRolesNote(roles);
    rolesStatus.textContent = `${roles.length} teams · ${result.phase}`;
  } catch (error) {
    showError(error.message);
    rolesStatus.textContent = "Error";
  } finally {
    buildRolesButton.disabled = false;
  }
}

buildRolesButton.addEventListener("click", buildRoles);

// ---- Alliance evaluator ----

function allianceCard(title, side, evaluation) {
  if (!evaluation) {
    return "";
  }
  const defense = evaluation.has_defense_data
    ? formatNumber(evaluation.best_defense, 1)
    : "n/a";
  const stat = (label, value) =>
    `<div class="alliance-stat"><span class="alliance-stat-value">${value}</span>` +
    `<span class="alliance-stat-label">${label}</span></div>`;
  return (
    `<div class="alliance-card ${side}">` +
    `<div class="alliance-card-head"><h3>${title}</h3>` +
    `<span class="alliance-teams">${(evaluation.teams || []).join(", ")}</span></div>` +
    `<div class="alliance-stats">` +
    stat("Predicted", formatNumber(evaluation.predicted_score, 1)) +
    stat("Synergy", formatNumber(evaluation.synergy_score, 1)) +
    stat("Auto", formatNumber(evaluation.auto, 1)) +
    stat("Teleop", formatNumber(evaluation.teleop, 1)) +
    stat("Endgame", formatNumber(evaluation.endgame, 1)) +
    stat("Best DPR", defense) +
    `</div>` +
    `<p class="alliance-note">${evaluation.note || ""}</p>` +
    `</div>`
  );
}

function renderAlliance(result) {
  const cards = [allianceCard("Alliance", "red", result.alliance)];
  if (result.opponent) {
    cards.push(allianceCard("Opponent", "blue", result.opponent));
  }
  let matchup = "";
  if (result.opponent && Number.isFinite(Number(result.red_win_probability))) {
    const redWin = Number(result.red_win_probability);
    const margin = Number(result.adjusted_score_diff) || 0;
    const favored = margin >= 0 ? "Alliance" : "Opponent";
    matchup =
      `<p class="alliance-matchup">${favored} favored · ` +
      `win prob ${formatPercent(redWin)} / ${formatPercent(result.blue_win_probability)} · ` +
      `margin ${formatNumber(Math.abs(margin), 1)} pts</p>`;
  }
  allianceResult.innerHTML = `<div class="alliance-cards">${cards.join("")}</div>${matchup}`;
  allianceResult.hidden = false;
}

async function evaluateAlliance() {
  clearError();
  allianceStatus.textContent = "Evaluating...";
  evalAllianceButton.disabled = true;
  try {
    const result = await postJson("/api/alliance/run", {
      event_key: eventInput.value,
      alliance: allianceTeams.value,
      vs: allianceVs.value
    });
    renderAlliance(result);
    allianceStatus.textContent = "Done";
  } catch (error) {
    showError(error.message);
    allianceStatus.textContent = "Error";
  } finally {
    evalAllianceButton.disabled = false;
  }
}

evalAllianceButton.addEventListener("click", evaluateAlliance);

refreshFiles();
