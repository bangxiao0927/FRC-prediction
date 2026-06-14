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
const evalAllianceButton = document.getElementById("evalAlliance");
const allianceStatus = document.getElementById("allianceStatus");
const allianceResult = document.getElementById("allianceResult");
const allianceChartBox = document.getElementById("allianceChartBox");
const loadEventButton = document.getElementById("loadEvent");
const teamOptionsList = document.getElementById("teamOptions");
const eventSearchBox = document.getElementById("eventSearchBox");
const eventSuggestions = document.getElementById("eventSuggestions");
const yearSelect = document.getElementById("yearSelect");
const eventSelect = document.getElementById("eventSelect");
const allianceTeamSelects = [
  document.getElementById("allianceTeam1"),
  document.getElementById("allianceTeam2"),
  document.getElementById("allianceTeam3")
];
const vsTeamSelects = [
  document.getElementById("vsTeam1"),
  document.getElementById("vsTeam2"),
  document.getElementById("vsTeam3")
];

let chart;
let picklistChart;
let rolesChart;
let allianceChart;
let autoTimer = null;
let isBusy = false;
let availableEvents = [];
let remoteEvents = [];
let visibleEventSuggestions = [];
let activeEventSuggestion = -1;
let eventSearchTimer = null;
let eventSearchRequestId = 0;
let isSearchingEvents = false;

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

// ---- Event-driven dropdowns ----

function joinTeamSelects(selects) {
  return selects
    .map((select) => (select ? select.value.trim() : ""))
    .filter((value) => value !== "")
    .join(",");
}

function teamOptionLabel(team) {
  const number = team.team_number || team.key;
  return team.nickname ? `${number} · ${team.nickname}` : String(number);
}

function eventOptionLabel(event) {
  return `${event.key} · ${event.name}`;
}

function eventYearLabel(event) {
  return event.year ? String(event.year) : "Current year";
}

function normalizeSearchText(value) {
  return String(value || "")
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, " ")
    .trim();
}

function compactSearchText(value) {
  return normalizeSearchText(value).replace(/\s+/g, "");
}

function subsequencePenalty(query, text) {
  if (!query || !text) {
    return null;
  }
  let cursor = 0;
  let penalty = 0;
  for (const char of query) {
    const position = text.indexOf(char, cursor);
    if (position === -1) {
      return null;
    }
    penalty += position - cursor;
    cursor = position + 1;
  }
  return penalty + (text.length - query.length);
}

function scoreEventMatch(event, query) {
  const normalizedQuery = normalizeSearchText(query);
  if (!normalizedQuery) {
    return 1;
  }

  const compactQuery = compactSearchText(query);
  const key = String(event.key || "");
  const name = String(event.name || "");
  const keyNormalized = normalizeSearchText(key);
  const nameNormalized = normalizeSearchText(name);
  const combined = `${keyNormalized} ${nameNormalized}`.trim();
  const compactKey = compactSearchText(key);
  const compactName = compactSearchText(name);
  const tokens = normalizedQuery.split(/\s+/).filter(Boolean);

  let bestScore = null;
  const consider = (score) => {
    if (score === null) {
      return;
    }
    if (bestScore === null || score > bestScore) {
      bestScore = score;
    }
  };

  if (compactKey === compactQuery) {
    return 5000;
  }
  if (combined === normalizedQuery) {
    return 4900;
  }
  if (compactKey.startsWith(compactQuery)) {
    consider(4600 - compactKey.length);
  }
  if (nameNormalized.startsWith(normalizedQuery)) {
    consider(4400 - nameNormalized.length);
  }

  const tokenPositions = tokens.map((token) => combined.indexOf(token));
  if (tokens.length > 0 && tokenPositions.every((position) => position !== -1)) {
    consider(4000 - tokenPositions.reduce((sum, position) => sum + position, 0));
  }

  const combinedIndex = combined.indexOf(normalizedQuery);
  if (combinedIndex !== -1) {
    consider(3600 - combinedIndex);
  }

  const keyPenalty = subsequencePenalty(compactQuery, compactKey);
  if (keyPenalty !== null) {
    consider(3200 - keyPenalty);
  }

  const namePenalty = subsequencePenalty(compactQuery, compactName);
  if (namePenalty !== null) {
    consider(2800 - namePenalty);
  }

  return bestScore;
}

function syncEventSelect(eventKey) {
  if (Array.from(eventSelect.options).some((option) => option.value === eventKey)) {
    eventSelect.value = eventKey;
  } else {
    eventSelect.value = "";
  }
}

function hasLoadedEvent(eventKey) {
  return availableEvents.some((event) => event.key === eventKey);
}

function uniqueEvents(events) {
  const seen = new Set();
  return events.filter((event) => {
    if (!event || !event.key || seen.has(event.key)) {
      return false;
    }
    seen.add(event.key);
    return true;
  });
}

function escapeHtml(value) {
  return String(value || "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#39;");
}

function escapeRegex(value) {
  return String(value || "").replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

function highlightMatches(text, query) {
  const tokens = normalizeSearchText(query)
    .split(/\s+/)
    .filter((token) => token.length > 0)
    .sort((left, right) => right.length - left.length);

  if (tokens.length === 0) {
    return escapeHtml(text);
  }

  const pattern = tokens.map(escapeRegex).join("|");
  if (!pattern) {
    return escapeHtml(text);
  }

  const source = String(text || "");
  const matches = [];
  const regex = new RegExp(pattern, "ig");
  let match;
  while ((match = regex.exec(source)) !== null) {
    matches.push({ start: match.index, end: match.index + match[0].length });
  }
  if (matches.length === 0) {
    return escapeHtml(source);
  }

  matches.sort((left, right) => left.start - right.start || right.end - left.end);
  const merged = [];
  matches.forEach((current) => {
    const previous = merged[merged.length - 1];
    if (!previous || current.start > previous.end) {
      merged.push(current);
      return;
    }
    previous.end = Math.max(previous.end, current.end);
  });

  let cursor = 0;
  let html = "";
  merged.forEach((range) => {
    html += escapeHtml(source.slice(cursor, range.start));
    html += `<mark>${escapeHtml(source.slice(range.start, range.end))}</mark>`;
    cursor = range.end;
  });
  html += escapeHtml(source.slice(cursor));
  return html;
}

function setEventSearchLoading(isLoading) {
  isSearchingEvents = isLoading;
  if (document.activeElement === eventInput) {
    renderEventSuggestions();
  }
}

async function searchEventsAcrossYears(query) {
  const trimmedQuery = query.trim();
  if (trimmedQuery.length < 2) {
    remoteEvents = [];
    setEventSearchLoading(false);
    return;
  }

  const requestId = eventSearchRequestId + 1;
  eventSearchRequestId = requestId;
  setEventSearchLoading(true);
  try {
    const result = await postJson("/api/events/search", {
      query: trimmedQuery,
      year_hint: Number(yearSelect.value) || undefined,
      limit: 12
    });
    if (requestId !== eventSearchRequestId || eventInput.value.trim() !== trimmedQuery) {
      return;
    }
    remoteEvents = result.events || [];
  } catch (error) {
    if (requestId !== eventSearchRequestId) {
      return;
    }
    remoteEvents = [];
  } finally {
    if (requestId === eventSearchRequestId) {
      setEventSearchLoading(false);
    }
  }
}

function scheduleEventSearch(query) {
  if (eventSearchTimer !== null) {
    clearTimeout(eventSearchTimer);
    eventSearchTimer = null;
  }

  if (query.trim().length < 2) {
    eventSearchRequestId += 1;
    remoteEvents = [];
    setEventSearchLoading(false);
    return;
  }

  eventSearchTimer = setTimeout(() => {
    eventSearchTimer = null;
    searchEventsAcrossYears(query);
  }, 160);
}

function hideEventSuggestions() {
  visibleEventSuggestions = [];
  activeEventSuggestion = -1;
  eventSuggestions.hidden = true;
  eventSuggestions.innerHTML = "";
  eventInput.setAttribute("aria-expanded", "false");
  eventInput.removeAttribute("aria-activedescendant");
}

function renderEventSuggestions() {
  const allEvents = uniqueEvents([...availableEvents, ...remoteEvents]);
  const query = eventInput.value.trim();
  if (!query && availableEvents.length === 0) {
    hideEventSuggestions();
    return;
  }
  if (allEvents.length === 0) {
    if (isSearchingEvents && query) {
      eventSuggestions.innerHTML = '<div class="event-suggestion-empty">Searching events…</div>';
      eventSuggestions.hidden = false;
      eventInput.setAttribute("aria-expanded", "true");
      eventInput.removeAttribute("aria-activedescendant");
      return;
    }
    hideEventSuggestions();
    return;
  }

  if (!query) {
    visibleEventSuggestions = availableEvents.slice(0, 8);
  } else {
    visibleEventSuggestions = allEvents
      .map((event, index) => ({ event, index, score: scoreEventMatch(event, query) }))
      .filter((item) => item.score !== null)
      .sort((left, right) => right.score - left.score || left.index - right.index)
      .slice(0, 8)
      .map((item) => item.event);
  }

  eventSuggestions.innerHTML = "";
  if (visibleEventSuggestions.length === 0) {
    eventSuggestions.innerHTML = `<div class="event-suggestion-empty">${isSearchingEvents ? "Searching events…" : "No matching events"}</div>`;
    eventSuggestions.hidden = false;
    eventInput.setAttribute("aria-expanded", "true");
    eventInput.removeAttribute("aria-activedescendant");
    activeEventSuggestion = -1;
    return;
  }

  activeEventSuggestion = Math.min(activeEventSuggestion, visibleEventSuggestions.length - 1);
  visibleEventSuggestions.forEach((event, index) => {
    const option = document.createElement("button");
    option.type = "button";
    option.id = `eventSuggestion-${index}`;
    option.className = "event-suggestion";
    option.setAttribute("role", "option");
    option.setAttribute("aria-selected", index === activeEventSuggestion ? "true" : "false");
    if (index === activeEventSuggestion) {
      option.classList.add("is-active");
    }
    option.innerHTML =
      `<span class="event-suggestion-top">` +
      `<span class="event-suggestion-key">${highlightMatches(event.key, query)}</span>` +
      `<span class="event-suggestion-year">${escapeHtml(eventYearLabel(event))}</span>` +
      `</span>` +
      `<span class="event-suggestion-name">${highlightMatches(event.name, query)}</span>`;
    option.addEventListener("mousedown", async (eventObject) => {
      eventObject.preventDefault();
      await applyEventSuggestion(index, true);
    });
    eventSuggestions.appendChild(option);
  });

  eventSuggestions.hidden = false;
  eventInput.setAttribute("aria-expanded", "true");
  if (activeEventSuggestion >= 0) {
    eventInput.setAttribute("aria-activedescendant", `eventSuggestion-${activeEventSuggestion}`);
  } else {
    eventInput.removeAttribute("aria-activedescendant");
  }
}

function openEventSuggestions() {
  renderEventSuggestions();
  scheduleEventSearch(eventInput.value);
}

function moveEventSuggestion(step) {
  if (visibleEventSuggestions.length === 0) {
    openEventSuggestions();
    if (visibleEventSuggestions.length === 0) {
      return;
    }
  }
  if (activeEventSuggestion === -1) {
    activeEventSuggestion = step > 0 ? 0 : visibleEventSuggestions.length - 1;
  } else {
    activeEventSuggestion = (activeEventSuggestion + step + visibleEventSuggestions.length) % visibleEventSuggestions.length;
  }
  renderEventSuggestions();
}

async function applyEventSuggestion(index, shouldLoad) {
  const event = visibleEventSuggestions[index];
  if (!event) {
    return;
  }
  eventInput.value = event.key;
  hideEventSuggestions();
  remoteEvents = [];
  if (event.year && String(event.year) !== yearSelect.value) {
    yearSelect.value = String(event.year);
    await loadEventsForYear();
    hideEventSuggestions();
  }
  syncEventSelect(event.key);
  if (shouldLoad) {
    loadEventOptionsIfChanged();
  }
}

// Fill a <select> with team options, preserving the current value when possible.
function fillTeamSelect(select, teams, placeholder) {
  if (!select) {
    return;
  }
  const previous = select.value;
  select.innerHTML = "";
  const blank = document.createElement("option");
  blank.value = "";
  blank.textContent = placeholder;
  select.appendChild(blank);
  teams.forEach((team) => {
    const option = document.createElement("option");
    option.value = team.key;
    option.textContent = teamOptionLabel(team);
    select.appendChild(option);
  });
  // Restore the prior selection if that team is still in the new event.
  if (teams.some((team) => team.key === previous)) {
    select.value = previous;
  }
}

function fillMatchSelect(matches) {
  const previous = matchInput.value;
  matchInput.innerHTML = "";
  const upcoming = document.createElement("option");
  upcoming.value = "";
  upcoming.textContent = "Upcoming / latest";
  matchInput.appendChild(upcoming);
  matches.forEach((match) => {
    const option = document.createElement("option");
    option.value = match.key;
    option.textContent = match.played ? match.label : `${match.label} (upcoming)`;
    matchInput.appendChild(option);
  });
  if (matches.some((match) => match.key === previous)) {
    matchInput.value = previous;
  }
}

async function loadEventOptions() {
  const eventKey = eventInput.value.trim();
  if (!eventKey) {
    return;
  }
  statusLabel.textContent = "Loading event…";
  eventInput.dataset.pendingLoad = eventKey;
  loadEventButton.disabled = true;
  loadEventButton.classList.add("is-busy");
  try {
    const options = await postJson("/api/event/options", { event_key: eventKey });
    const teams = options.teams || [];
    const matches = options.matches || [];

    fillMatchSelect(matches);
    fillTeamSelect(picklistTeam, teams, "Select team…");
    allianceTeamSelects.forEach((select, index) =>
      fillTeamSelect(select, teams, `Robot ${index + 1}`));
    vsTeamSelects.forEach((select, index) =>
      fillTeamSelect(select, teams, `Robot ${index + 1}`));

    // Datalist suggestions for the remaining free-text team fields.
    teamOptionsList.innerHTML = "";
    teams.forEach((team) => {
      const option = document.createElement("option");
      option.value = team.key;
      option.label = teamOptionLabel(team);
      teamOptionsList.appendChild(option);
    });

    syncEventSelect(eventKey);

    statusLabel.textContent = `Loaded ${teams.length} teams · ${matches.length} matches`;
    eventInput.dataset.lastLoaded = eventKey;
  } catch (error) {
    // Non-fatal: the dashboard still works with manual entry / current files.
    statusLabel.textContent = `Could not load event options: ${error.message}`;
  } finally {
    delete eventInput.dataset.pendingLoad;
    loadEventButton.disabled = false;
    loadEventButton.classList.remove("is-busy");
  }
}

// ---- Year -> event dropdown ----

function populateYears() {
  const latest = new Date().getFullYear();
  yearSelect.innerHTML = "";
  for (let year = latest; year >= 2010; year -= 1) {
    const option = document.createElement("option");
    option.value = String(year);
    option.textContent = String(year);
    yearSelect.appendChild(option);
  }
  // Default to the year embedded in the current event key, else the latest.
  const fromKey = (eventInput.value.match(/^(\d{4})/) || [])[1];
  yearSelect.value = fromKey && Number(fromKey) <= latest ? fromKey : String(latest);
}

async function loadEventsForYear() {
  const year = Number(yearSelect.value);
  if (!year) {
    return;
  }
  eventSelect.disabled = true;
  statusLabel.textContent = `Loading ${year} events…`;
  try {
    const data = await postJson("/api/events", { year });
    const events = (data.events || []).map((event) => ({ ...event, year: event.year || year }));
    availableEvents = events;
    const previous = eventSelect.value;
    const currentKey = eventInput.value.trim();
    eventSelect.innerHTML = "";
    const blank = document.createElement("option");
    blank.value = "";
    blank.textContent = "Select event…";
    eventSelect.appendChild(blank);
    events.forEach((event) => {
      const option = document.createElement("option");
      option.value = event.key;
      option.textContent = eventOptionLabel(event);
      eventSelect.appendChild(option);
    });
    if (events.some((event) => event.key === currentKey)) {
      eventSelect.value = currentKey;
    } else if (events.some((event) => event.key === previous)) {
      eventSelect.value = previous;
    } else {
      eventSelect.value = "";
    }
    if (document.activeElement === eventInput) {
      openEventSuggestions();
    }
    statusLabel.textContent = `Loaded ${events.length} ${year} events`;
  } catch (error) {
    availableEvents = [];
    hideEventSuggestions();
    statusLabel.textContent = `Could not load events: ${error.message}`;
  } finally {
    eventSelect.disabled = false;
  }
}

function loadEventOptionsIfChanged(options = {}) {
  const requireKnownEvent = Boolean(options.requireKnownEvent);
  const eventKey = eventInput.value.trim();
  syncEventSelect(eventKey);
  if (requireKnownEvent && availableEvents.length > 0 && !hasLoadedEvent(eventKey)) {
    return;
  }
  if (!eventKey || eventKey === eventInput.dataset.lastLoaded || eventKey === eventInput.dataset.pendingLoad) {
    return;
  }
  loadEventOptions();
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
loadEventButton.addEventListener("click", loadEventOptions);
yearSelect.addEventListener("change", async () => {
  await loadEventsForYear();
  if (document.activeElement === eventInput) {
    scheduleEventSearch(eventInput.value);
  }
});
// Choosing an event from the dropdown drives the event key + its options.
eventSelect.addEventListener("change", () => {
  if (eventSelect.value) {
    eventInput.value = eventSelect.value;
    hideEventSuggestions();
    loadEventOptionsIfChanged();
  }
});
eventInput.addEventListener("focus", openEventSuggestions);
eventInput.addEventListener("input", () => {
  activeEventSuggestion = -1;
  syncEventSelect(eventInput.value.trim());
  openEventSuggestions();
});
eventInput.addEventListener("blur", () => {
  hideEventSuggestions();
  loadEventOptionsIfChanged({ requireKnownEvent: true });
});
eventInput.addEventListener("change", () => {
  loadEventOptionsIfChanged({ requireKnownEvent: true });
});
eventInput.addEventListener("keydown", (event) => {
  if (event.key === "ArrowDown") {
    event.preventDefault();
    moveEventSuggestion(1);
    return;
  }
  if (event.key === "ArrowUp") {
    event.preventDefault();
    moveEventSuggestion(-1);
    return;
  }
  if (event.key === "Escape") {
    hideEventSuggestions();
    return;
  }
  if (event.key === "Enter") {
    if (!eventSuggestions.hidden && activeEventSuggestion >= 0) {
      event.preventDefault();
      applyEventSuggestion(activeEventSuggestion, true);
      return;
    }
    hideEventSuggestions();
    loadEventOptionsIfChanged();
  }
});
document.addEventListener("mousedown", (event) => {
  if (!eventSearchBox.contains(event.target)) {
    hideEventSuggestions();
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

function renderRolesChart(roles) {
  if (typeof Chart === "undefined") {
    return;
  }
  // Top teams by offense, with their phase contributions stacked so you can see
  // where each robot's output comes from.
  const top = roles.slice(0, 15);
  const labels = top.map((role) => role.team_key);
  const phase = (key) => top.map((role) => Number(role[key]) || 0);

  const ctx = document.getElementById("rolesChart").getContext("2d");
  if (rolesChart) {
    rolesChart.destroy();
  }
  rolesChart = new Chart(ctx, {
    type: "bar",
    data: {
      labels,
      datasets: [
        { label: "Auto", data: phase("auto"), backgroundColor: "rgba(37, 99, 235, 0.8)" },
        { label: "Teleop", data: phase("teleop"), backgroundColor: "rgba(16, 185, 129, 0.8)" },
        { label: "Endgame", data: phase("endgame"), backgroundColor: "rgba(109, 40, 217, 0.8)" }
      ]
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      plugins: { legend: { position: "bottom" } },
      scales: { x: { stacked: true }, y: { stacked: true, beginAtZero: true } }
    }
  });
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
    renderRolesChart(roles);
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
  renderAllianceChart(result);
}

function renderAllianceChart(result) {
  if (typeof Chart === "undefined" || !allianceChartBox) {
    return;
  }
  // Compare the alliance (and optional opponent) across scoring phases.
  const labels = ["Auto", "Teleop", "Endgame"];
  const phaseData = (evaluation) =>
    evaluation ? [Number(evaluation.auto) || 0, Number(evaluation.teleop) || 0, Number(evaluation.endgame) || 0] : null;
  const datasets = [
    { label: "Alliance", data: phaseData(result.alliance), backgroundColor: "rgba(220, 38, 38, 0.8)" }
  ];
  if (result.opponent) {
    datasets.push({ label: "Opponent", data: phaseData(result.opponent), backgroundColor: "rgba(37, 99, 235, 0.8)" });
  }

  const ctx = document.getElementById("allianceChart").getContext("2d");
  if (allianceChart) {
    allianceChart.destroy();
  }
  allianceChart = new Chart(ctx, {
    type: "bar",
    data: { labels, datasets },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      plugins: { legend: { position: "bottom" } },
      scales: { y: { beginAtZero: true } }
    }
  });
  allianceChartBox.hidden = false;
}

async function evaluateAlliance() {
  clearError();
  allianceStatus.textContent = "Evaluating...";
  evalAllianceButton.disabled = true;
  try {
    const result = await postJson("/api/alliance/run", {
      event_key: eventInput.value,
      alliance: joinTeamSelects(allianceTeamSelects),
      vs: joinTeamSelects(vsTeamSelects)
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

// Populate the year list and the default event's dropdowns, then load any
// existing files. The year -> events fetch hits TBA, so run it independently.
populateYears();
loadEventsForYear();
loadEventOptions();
refreshFiles();
