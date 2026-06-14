# FRC Prediction

[![CI](https://github.com/bangxiao0927/FRC-prediction/actions/workflows/ci.yml/badge.svg)](https://github.com/bangxiao0927/FRC-prediction/actions/workflows/ci.yml)

FRC Prediction is a data-driven assistant for FRC events. The goal is to deliver real-time win probability predictions during qualification matches and provide picklist/alliance recommendations for elimination and alliance selection.

**Languages:** English (default), [中文说明](README.zh-CN.md).

## Core Goals

- **Qualification win probability**: Predict win/loss and estimated margin based on event stats, team form, and alliance composition.
- **Elimination performance modeling**: Estimate alliance strength by combining team averages, consistency, and complementary roles.
- **Picklist recommendations**: Rank candidate teams based on strategic fit and predicted alliance performance.
- **Extensible data pipeline**: Pull official data from The Blue Alliance (TBA) and allow future scouting inputs.

## Tech Stack

- **Language**: C++17
- **Build**: CMake
- **Dependency manager**: vcpkg (manifest mode)
- **HTTP**: `cpr`
- **JSON**: `nlohmann/json`

We use C++ for performance and real-time compute needs, and vcpkg to simplify third-party libraries on macOS.

## Data Sources

- The Blue Alliance API: events, teams, matches, rankings, and scores
- Future extension: on-site scouting data (scoring ability, defense, errors, reliability)

## Modeling Scope

**No future-data leakage.** A prediction for a match only uses matches scheduled
strictly before it (qualification predictions use earlier qualification matches;
elimination predictions add played elimination matches). This means a team's
average and match count reflect what was known at match time, so backtests
(`--evaluate`) and live predictions use the exact same information.

### Qualification Stage

Inputs:

- Event-specific team performance (completed matches)
- Recent historical team performance
- Red/blue alliance composition
- Schedule and score data from TBA

Outputs:

- Red/blue win probability
- Estimated score margin
- Confidence hints when sample size is small

### Elimination / Picklist Stage

Inputs:

- Qualification averages, consistency, and trends
- Tactical roles (offense/defense/endgame/special tasks)
- Estimated alliance synergy

Outputs:

- Picklist ranking
- Alliance score estimates
- Match win probability

The CLI `--picklist` ranks teams from qualification play using a weighted mix of
strength (average score), consistency (low score variance), and trend (recent vs
early improvement), damped by how many matches a team has played. Use
`--strategy balanced|offense|consistency` to change the mix, `--exclude` to drop
already-picked teams, and `--before MATCH_KEY` to rank as of a point in the event.

## Project Structure

```text
.
├── CMakeLists.txt
├── ISSUES.md
├── LICENSE
├── PLAN.md
├── README.md
├── README.zh-CN.md
├── config.example.json
├── config.json
├── app.py
├── requirements.txt
├── requirements-dev.txt
├── src/
│   ├── main.cpp
│   ├── cache.{h,cpp}
│   ├── config.{h,cpp}
│   ├── history.{h,cpp}
│   ├── opr.{h,cpp}
│   ├── picklist.{h,cpp}
│   ├── predictor.{h,cpp}
│   ├── roles.{h,cpp}
│   ├── stats.{h,cpp}
│   ├── synergy.{h,cpp}
│   └── tba_client.{h,cpp}
├── tests/
│   ├── predictor_tests.cpp
│   └── test_app.py
├── web/
│   ├── app.js
│   ├── index.html
│   └── styles.css
└── vcpkg.json
```

## Quick Start

### 1. Install prerequisites

Install CMake if needed:

```bash
brew install cmake
```

Install vcpkg:

```bash
git clone https://github.com/microsoft/vcpkg.git ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh
export VCPKG_ROOT=~/vcpkg
```

Add `export VCPKG_ROOT=~/vcpkg` to your shell profile (e.g. `~/.zshrc`).

### 2. Configure TBA API Key

Option A: environment variable

```bash
export TBA_AUTH_KEY=your_key_here
```

Option B: local config file

```bash
cp config.example.json config.json
```

Edit `config.json`:

```json
{
  "tba_auth_key": "your_key_here",
  "default_event_key": "2024casj",
  "cache_dir": "data/cache",
  "cache_ttl_seconds": 60,
  "confidence_match_count": 6,
  "score_diff_scale": 30.0,
  "sigmoid_scale": 1.0,
  "model_version": "baseline-v1",
  "use_opr": true
}
```

`config.json` is ignored by Git to avoid leaking API keys.
Responses are cached under `cache_dir` for a short TTL to reduce duplicate API calls.

`use_opr` (default `true`) selects the scoring model. With OPR enabled, each
team's contribution is solved by least squares over alliance scores (an Offensive
Power Rating), so an alliance estimate is the sum of its members' OPRs and is
directly comparable to a real score. Set it to `false` to fall back to the legacy
"sum of alliance averages" proxy. The OPR solve honors the same `--before`
schedule cutoff, so backtests never leak future matches.

### Team Roles (`--roles`)

`--roles` profiles each team's tactical contribution using the same
least-squares machinery as OPR:

- **offense**: total scoring contribution (OPR).
- **auto / teleop / endgame**: per-phase contributions, decomposed from each
  alliance's `score_breakdown` (they sum back to the offense, foul points aside).
- **defense**: a Defensive Power Rating computed by attributing the *opponent's*
  score to the alliance's teams, so a lower value means opponents scored less
  with this team on the field.

Each team is tagged a `primary` role (`offense`, `auto`, `endgame`, or
`defense`) based on how far it stands out from the field. Phase ratings need
`score_breakdown` data (available for recent seasons); without it, phase values
are `0` and roles fall back to offense/defense. Respects `--phase`, `--before`,
`--top`, and `--json`.

### Alliance Evaluation (`--alliance` / `--vs`)

`--alliance frcA,frcB,frcC` evaluates a hand-picked lineup as a what-if (using
every played match at the event):

- **predicted_score**: the OPR-based alliance estimate (sum of member OPRs).
- **auto / teleop / endgame / best_defense**: the lineup's combined role profile.
- **synergy_score**: the predicted score plus a transparent complementarity
  adjustment — a bonus for covering more distinct roles and carrying a defender,
  and a penalty for stacking redundant endgame specialists. The OPR estimate
  stays the headline; synergy only breaks ties between similar lineups.

Add `--vs frcD,frcE,frcF` to simulate a matchup: both lineups are evaluated and
the match predictor returns a win probability and estimated margin. Works with
`--json` for scripting.

### Cross-event history (`--use-history`)

By default a prediction only uses the current event's matches. Pass
`--use-history` (or set `use_history: true` in `config.json`) to blend in each
team's **prior-season form** from its other events that year:

- For every team in the match, the CLI pulls its season matches and computes the
  team's **scoring OPR at each *other* event** it played that year (restricted to
  matches before this one), then averages those event OPRs weighted by matches
  played. The scoring OPR sums the team's auto + teleop + endgame phase
  contributions, so it **excludes foul points** (awarded for the opponent's
  infractions, not a stable trait of the robot) and reflects the robot's own
  output. Running a real OPR per event also deconvolves teammates. (It falls back
  to total OPR for an event with no `score_breakdown`, and to a per-team score
  average if no event OPR can be derived.) Everything is restricted to matches
  played strictly *before* this match, so there is no future-data leakage, and a
  team's first event of the season has no history.
- The historical prior is blended with the team's current-event OPR **per phase**:
  each of auto / teleop / endgame gets its own confidence weight, so a phase that
  stabilizes quickly locks onto current form sooner than a noisier one. The counts
  are configurable (`history_auto_matches` / `history_teleop_matches` /
  `history_endgame_matches`, default 4 / 8 / 6) — a team at or above a phase's
  count is trusted entirely on current form for that phase, while a team with no
  current data falls back to history. Any non-phase points the OPR attributes
  (e.g. fouls) pass through unblended. Teams without a usable phase breakdown fall
  back to a single-weight blend of the total (`confidence_match_count`). This
  sharpens early-event estimates when the current sample is thin.
- Use `--history-teams frc254,frc1678` to blend history for **only those robots**
  (every other team keeps its pure current-event OPR). Passing `--history-teams`
  also turns history on, so you don't need `--use-history` as well.

Note: the win-probability confidence shrink still scales with the *current*-event
sample, so very early win probabilities stay conservative even though the score
estimate already reflects history. `--use-history` makes extra TBA calls per
team (cached), so it is opt-in.

### Backtesting accuracy (`--evaluate`)

`--evaluate` replays every completed match at an event, scoring each one using
only the matches before it (no future-data leakage), and reports **MAE** (mean
absolute error of the predicted score margin) and **winner accuracy**. Add
`--phase qm|elim|all` to scope it and `--eval-json` / `--eval-csv` to record runs.

Add `--use-history` to score every match a *second* time with the cross-event
history blend and print both side by side, so you can measure whether history
actually helps:

```text
  mae=19.11
  winner_accuracy=0.701
  history_mae=18.70 (improvement 0.41)
  history_winner_accuracy=0.714 (improvement 0.013)
```

(With `--eval-csv`, baseline and history are written as separate rows tagged in a
`model` column.) Because history makes per-team TBA calls for every match, this
is slow on a cold cache.

### 3. Build

```bash
cmake -B build -S . \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build
```

Run the tests (CI runs both on every push/PR):

```bash
ctest --test-dir build --output-on-failure   # C++ unit tests
pip install -r requirements-dev.txt
python -m pytest tests/test_app.py -q          # Flask endpoint tests (offline, CLI mocked)
```

### 4. Run

```bash
./build/frc_prediction --status
```

### Web Dashboard (Flask)

1. Build the CLI (the dashboard calls `build/frc_prediction`):

```bash
cmake --build build
```

2. Start the web server:

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
python app.py
```

3. Open `http://127.0.0.1:5001`, enter an event key (and optionally a match),
   then click **Run**. The dashboard runs the CLI for you — no need to generate
   files by hand.

### Docker Deployment

For servers or quick deployment without installing vcpkg locally:

```bash
export TBA_AUTH_KEY=your_key_here
docker compose up -d
```

The dashboard will be available at `http://<server>:8000`. The image builds
the C++ CLI from source and serves the Flask app with gunicorn. Data is
persisted in a Docker volume.

Manual build without compose:

```bash
docker build -t frc-prediction .
docker run -d -p 8000:8000 -e TBA_AUTH_KEY=your_key -v frc_data:/app/data frc-prediction
```

### Ubuntu Server Deployment (bare-metal)

One-command setup for Ubuntu 22.04 / 24.04:

```bash
git clone https://github.com/bangxiao0927/FRC-prediction.git
cd FRC-prediction
chmod +x deploy/setup.sh
./deploy/setup.sh
```

This installs all dependencies, builds the CLI, creates a systemd service, and
configures nginx as a reverse proxy. After setup:

```bash
# Edit your TBA key
nano ~/frc-prediction/config.json

# Restart the service
sudo systemctl restart frc-prediction

# View logs
sudo journalctl -u frc-prediction -f
```

Dashboard available at `http://<server-ip>`.

The dashboard provides:

- A **win-probability bar** plus a red-left / blue-right comparison of each
  alliance (estimated score, win probability, confidence, average matches).
- A **Team Stats** table and chart for the event; the selected match's teams are
  highlighted (red/blue) and listed above the chart with their averages.
- **Match shorthand**: type `3`, `qm3`, or `sf2m1` instead of the full key.
- **Auto-refresh** with a configurable interval to keep predictions current
  during a live event.
- A **Picklist** section: pick a strategy (balanced/offense/consistency), exclude
  already-picked teams, and build a ranked table + chart. It reuses the Event and
  Match fields (Match acts as an "as of" cutoff).
- A **History** toggle next to Run: blend cross-event prior-season form into the
  prediction, optionally scoped to specific robots via the **History teams** box
  (empty = all). A badge on the prediction shows when history is applied.
- A **Team Roles** section: per-team offense / auto / teleop / endgame and a
  defensive DPR, with a primary-role badge (honors the Match field as an "as of"
  cutoff and a phase filter).
- An **Alliance Evaluator**: enter a hand-picked lineup (and an optional opponent)
  to get OPR-based predicted + synergy scores, phase totals, best defender, and a
  what-if matchup win probability.
- **Pick-from-event dropdowns**: choose a **Year** then an **Event** (populated
  from TBA), and Match + team selectors fill from that event's real schedule and
  roster — Match becomes a dropdown, the alliance/opponent are three robot pickers
  each, the picklist team is a dropdown, and the free-text team fields (exclude /
  history teams) get autocomplete suggestions. Controls disable while loading.

Examples:

```bash
./build/frc_prediction --event 2024casj --matches
./build/frc_prediction --event 2024casj --rankings
./build/frc_prediction --event 2024casj --teams
./build/frc_prediction --event 2024casj --stats
./build/frc_prediction --event 2024casj --stats --top 10
./build/frc_prediction --event 2024casj --stats-json --top 10
./build/frc_prediction --event 2024casj --stats --top 10 --stats-csv data/stats.csv
./build/frc_prediction --event 2024casj --roles --top 10
./build/frc_prediction --event 2024casj --roles --json --before qm40
./build/frc_prediction --event 2024casj --alliance frc1678,frc604,frc841
./build/frc_prediction --event 2024casj --alliance frc1678,frc604,frc841 --vs frc581,frc987,frc100
./build/frc_prediction --event 2024casj --predict 2024casj_qm1
./build/frc_prediction --event 2024casj --predict-upcoming
./build/frc_prediction --event 2024casj --predict-upcoming --json
./build/frc_prediction --event 2024casj --predict-upcoming --json --output data/prediction.json
./build/frc_prediction --event 2024casj --evaluate
./build/frc_prediction --event 2024casj --evaluate --phase qm
./build/frc_prediction --event 2024casj --evaluate --phase elim --eval-json data/eval.json
./build/frc_prediction --event 2024casj --evaluate --phase all --eval-csv data/eval.csv
./build/frc_prediction --event 2024cacc --evaluate --phase qm --use-history
./build/frc_prediction --event 2024casj --picklist frc254 --top 24 --strategy balanced
./build/frc_prediction --event 2024casj --picklist frc254 --top 24
./build/frc_prediction --event 2024casj --picklist frc254 --strategy offense --exclude 1678,254
./build/frc_prediction --event 2024casj --picklist frc254 --before qm40 --json
./build/frc_prediction --event 2024casj --picklist frc254 --picklist-csv data/picklist.csv

Default prediction output path when using --json without --output:

```
data/predictions/<match_key>.json
```

Prediction outputs now include team counts, average matches per alliance, and adjusted averages relative to the event.
Qualification predictions use qualification matches only; elimination predictions use qualification plus played elimination matches.
```

## Roadmap

- [x] Initialize CMake + vcpkg project
- [x] Add TBA API minimal request
- [x] Add local config template
- [x] Wrap a `TbaClient` class
- [x] Pull event matches and rankings
- [x] Add local cache
- [x] Compute baseline team statistics
- [x] Generate qualification win probabilities
- [x] Produce elimination picklist recommendations
- [x] Add backtesting (`--evaluate`) and history blending (`--use-history`)
- [x] Add team roles, alliance evaluator, and CLI JSON/CSV outputs
- [x] Ship a Flask dashboard for running predictions and picklists

## Completion Criteria (MVP)

This project is considered "done" for MVP when all of these are true:

- The CLI can predict qualification matches, generate picklists, and run
  `--evaluate` backtests.
- The Flask dashboard can run those same workflows end-to-end.
- C++ and Flask tests pass locally (or CI is green on the main branch).

## Open Issues

- [x] UI: optimized dashboard layout (tab navigation, zebra-striped tables,
  sticky headers, responsive breakpoints, Top defaults to event team count)

## Contributing

The project has reached MVP: the CLI predicts qualification matches, generates
picklists, and runs `--evaluate` backtests; the Flask dashboard runs the same
workflows end-to-end. Contributions for new features, model improvements, and UI
enhancements are welcome.

## Picklist Strategy (How Ranking Is Computed)

Picklist recommendations are built to maximize alliance fit rather than raw strength, and the output includes your team summary for context.

### Inputs

- Per-team average score from qualification-only matches (by default).
- Standard deviation of match scores (consistency).
- Recent trend from the last three completed matches.
- Your team key (to compute complement/overlap).

### Scores

For each candidate team:

```
strength    = candidate_avg / event_avg
consistency = 1 / (1 + stddev)
trend       = (recent_avg - candidate_avg) / event_avg

if my_avg >= event_avg:
  complement = event_avg / (candidate_avg + event_avg)  # favors support roles
else:
  complement = candidate_avg / (candidate_avg + event_avg)  # favors scoring partners

overlap_penalty = max(0, 1 - abs(candidate_avg - my_avg) / event_avg)

picklist_score =
  w_strength * strength
  + w_consistency * consistency
  + w_trend * trend
  + w_complement * complement
  - w_overlap * overlap_penalty
```

Strategy presets map to different weights:

- **balanced**: 0.45 strength, 0.25 consistency, 0.10 trend, 0.25 complement, 0.15 overlap
- **offense**: 0.60 strength, 0.15 consistency, 0.10 trend, 0.30 complement, 0.10 overlap
- **consistency**: 0.30 strength, 0.50 consistency, 0.10 trend, 0.30 complement, 0.10 overlap

### Usage

```
./build/frc_prediction --event 2024casj --picklist frc254 --top 24 --strategy balanced
```
