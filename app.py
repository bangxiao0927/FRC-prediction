from pathlib import Path
import csv
import subprocess
from datetime import datetime, timezone
from functools import lru_cache
from typing import Optional

from flask import Flask, jsonify, request, send_from_directory


BASE_DIR = Path(__file__).resolve().parent
DATA_DIR = BASE_DIR / "data"
WEB_DIR = BASE_DIR / "web"
BIN_PATH = BASE_DIR / "build" / "frc_prediction"
STATS_PATH = DATA_DIR / "stats.csv"
PREDICTION_PATH = DATA_DIR / "prediction.json"

app = Flask(__name__, static_folder=str(WEB_DIR))


def normalize_search_text(value: str) -> str:
    return " ".join("".join(
        ch.lower() if ch.isalnum() else " "
        for ch in str(value or "")
    ).split())


def compact_search_text(value: str) -> str:
    return normalize_search_text(value).replace(" ", "")


def subsequence_penalty(query: str, text: str):
    if not query or not text:
        return None
    cursor = 0
    penalty = 0
    for char in query:
        position = text.find(char, cursor)
        if position == -1:
            return None
        penalty += position - cursor
        cursor = position + 1
    return penalty + (len(text) - len(query))


def score_event_match(event: dict, query: str):
    normalized_query = normalize_search_text(query)
    if not normalized_query:
        return 1

    compact_query = compact_search_text(query)
    key = str(event.get("key", ""))
    name = str(event.get("name", ""))
    key_normalized = normalize_search_text(key)
    name_normalized = normalize_search_text(name)
    combined = f"{key_normalized} {name_normalized}".strip()
    compact_key = compact_search_text(key)
    compact_name = compact_search_text(name)
    tokens = [token for token in normalized_query.split(" ") if token]

    best_score = None

    def consider(score):
        nonlocal best_score
        if score is None:
            return
        if best_score is None or score > best_score:
            best_score = score

    if compact_key == compact_query:
        return 5000
    if combined == normalized_query:
        return 4900
    if compact_key.startswith(compact_query):
        consider(4600 - len(compact_key))
    if name_normalized.startswith(normalized_query):
        consider(4400 - len(name_normalized))

    token_positions = [combined.find(token) for token in tokens]
    if tokens and all(position != -1 for position in token_positions):
        consider(4000 - sum(token_positions))

    combined_index = combined.find(normalized_query)
    if combined_index != -1:
        consider(3600 - combined_index)

    key_penalty = subsequence_penalty(compact_query, compact_key)
    if key_penalty is not None:
        consider(3200 - key_penalty)

    name_penalty = subsequence_penalty(compact_query, compact_name)
    if name_penalty is not None:
        consider(2800 - name_penalty)

    return best_score


def search_years(year_hint: Optional[int] = None):
    latest = datetime.now(timezone.utc).year
    years = list(range(latest, 2009, -1))
    if year_hint and year_hint in years:
        years.remove(year_hint)
        years.insert(0, year_hint)
    return years


@lru_cache(maxsize=32)
def load_events_for_year(year: int):
    result = run_cli(["--events-year", str(year), "--json"])
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or "Failed to load events.")
    try:
        payload = app.json.loads(result.stdout)
    except ValueError as exc:
        raise ValueError(result.stdout.strip()) from exc
    return payload.get("events", [])


@app.get("/")
def index():
    return send_from_directory(WEB_DIR, "index.html")


@app.get("/api/stats")
def api_stats():
    if not STATS_PATH.exists():
        return jsonify({"error": "stats.csv not found"}), 404
    return send_from_directory(DATA_DIR, STATS_PATH.name)


@app.get("/api/prediction")
def api_prediction():
    if not PREDICTION_PATH.exists():
        return jsonify({"error": "prediction.json not found"}), 404
    return send_from_directory(DATA_DIR, PREDICTION_PATH.name)


@app.post("/api/prediction/run")
def api_run_prediction():
    payload = request.get_json(silent=True) or {}
    event_key = str(payload.get("event_key", "")).strip()
    match_key = str(payload.get("match_key", "")).strip()
    top = payload.get("top", 20)
    use_history = bool(payload.get("use_history", False))
    history_teams = str(payload.get("history_teams", "")).strip()

    try:
        top_count = max(1, min(int(top), 100))
    except (TypeError, ValueError):
        top_count = 20

    if not event_key:
        return jsonify({"error": "event_key is required"}), 400

    if not BIN_PATH.exists():
        return jsonify({"error": "build/frc_prediction not found. Run cmake --build build first."}), 500

    DATA_DIR.mkdir(parents=True, exist_ok=True)
    phase = infer_stats_phase(match_key)
    stats_args = [
        "--event", event_key,
        "--stats",
        "--top", str(top_count),
        "--stats-csv", str(STATS_PATH),
        "--phase", phase
    ]
    # When a specific match is selected, show stats as of just before it so the
    # whole dashboard reflects "up to that match".
    if match_key:
        stats_args.extend(["--before", match_key])
    stats_result = run_cli(stats_args)
    if stats_result.returncode != 0:
        return cli_error_response("Failed to generate team stats.", stats_result)

    predict_args = [
        "--event", event_key,
        "--json",
        "--output", str(PREDICTION_PATH)
    ]
    if match_key:
        predict_args.extend(["--predict", match_key])
    else:
        predict_args.append("--predict-upcoming")
    # Cross-event history is opt-in (extra TBA calls). --history-teams scopes it
    # to specific robots and implies --use-history on the CLI side.
    if history_teams:
        predict_args.extend(["--history-teams", history_teams])
    elif use_history:
        predict_args.append("--use-history")

    prediction_result = run_cli(predict_args)
    if prediction_result.returncode != 0:
        hint = ""
        if "No upcoming match found" in prediction_result.stderr:
            hint = "This event has no upcoming matches. Enter a specific match key, for example 2024casj_qm1."
        return cli_error_response("Failed to generate prediction.", prediction_result, hint)

    prediction = read_prediction_json()
    return jsonify({
        "event_key": event_key,
        "match_key": match_key,
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "stats": read_stats_rows(),
        "prediction": prediction,
        "match_team_stats": collect_match_team_stats(event_key, phase, prediction, match_key)
    })


@app.get("/assets/<path:filename>")
def assets(filename: str):
    return send_from_directory(WEB_DIR, filename)


@app.post("/api/picklist/run")
def api_run_picklist():
    payload = request.get_json(silent=True) or {}
    event_key = str(payload.get("event_key", "")).strip()
    team_key = str(payload.get("team_key", "")).strip()
    strategy = str(payload.get("strategy", "balanced")).strip() or "balanced"
    exclude = str(payload.get("exclude", "")).strip()
    before = str(payload.get("before", "")).strip()
    top = payload.get("top", 24)

    try:
        top_count = max(1, min(int(top), 100))
    except (TypeError, ValueError):
        top_count = 24

    if not event_key:
        return jsonify({"error": "event_key is required"}), 400
    if not team_key:
        return jsonify({"error": "team_key is required"}), 400
    if not BIN_PATH.exists():
        return jsonify({"error": "build/frc_prediction not found. Run cmake --build build first."}), 500

    args = [
        "--event", event_key,
        "--picklist", team_key,
        "--json",
        "--top", str(top_count),
        "--strategy", strategy
    ]
    if exclude:
        args.extend(["--exclude", exclude])
    if before:
        args.extend(["--before", before])

    result = run_cli(args)
    if result.returncode != 0:
        return cli_error_response("Failed to build picklist.", result)

    try:
        picklist = app.json.loads(result.stdout)
    except ValueError:
        return jsonify({"error": "Could not parse picklist output.",
                        "stdout": result.stdout.strip()}), 500
    return jsonify(picklist)


@app.post("/api/events")
def api_events_by_year():
    """All events in a season, for the year -> event dropdown."""
    payload = request.get_json(silent=True) or {}
    try:
        year = int(payload.get("year", 0))
    except (TypeError, ValueError):
        year = 0
    if year <= 0:
        return jsonify({"error": "a positive year is required"}), 400
    if not BIN_PATH.exists():
        return jsonify({"error": "build/frc_prediction not found. Run cmake --build build first."}), 500

    try:
        events = load_events_for_year(year)
    except RuntimeError as exc:
        return jsonify({"error": "Failed to load events.", "stderr": str(exc)}), 500
    except ValueError as exc:
        return jsonify({"error": "Could not parse events.",
                        "stdout": str(exc)}), 500
    return jsonify({"year": year, "events": events})


@app.post("/api/events/search")
def api_search_events():
    """Fuzzy search events across seasons for the custom event combobox."""
    payload = request.get_json(silent=True) or {}
    query = str(payload.get("query", "")).strip()
    if not query:
        return jsonify({"error": "query is required"}), 400

    try:
        limit = max(1, min(int(payload.get("limit", 8)), 20))
    except (TypeError, ValueError):
        limit = 8

    try:
        year_hint = int(payload.get("year_hint", 0) or 0)
    except (TypeError, ValueError):
        year_hint = 0

    if not BIN_PATH.exists():
        return jsonify({"error": "build/frc_prediction not found. Run cmake --build build first."}), 500

    scored = []
    errors = []
    for year in search_years(year_hint or None):
        try:
            events = load_events_for_year(year)
        except RuntimeError as exc:
            errors.append({"year": year, "stderr": str(exc)})
            continue
        except ValueError as exc:
            errors.append({"year": year, "stdout": str(exc)})
            continue

        for index, event in enumerate(events):
            score = score_event_match(event, query)
            if score is None:
                continue
            scored.append({
                "score": score,
                "index": index,
                "event": {
                    **event,
                    "year": event.get("year", year)
                }
            })

    scored.sort(key=lambda item: (-item["score"], -int(item["event"].get("year", 0)), item["index"]))
    return jsonify({
        "query": query,
        "events": [item["event"] for item in scored[:limit]],
        "errors": errors
    })


@app.post("/api/event/options")
def api_event_options():
    """Teams and matches for an event, for the dashboard's dropdowns."""
    payload = request.get_json(silent=True) or {}
    event_key = str(payload.get("event_key", "")).strip()
    if not event_key:
        return jsonify({"error": "event_key is required"}), 400
    if not BIN_PATH.exists():
        return jsonify({"error": "build/frc_prediction not found. Run cmake --build build first."}), 500

    result = run_cli(["--event", event_key, "--event-options", "--json"])
    if result.returncode != 0:
        return cli_error_response("Failed to load event options.", result)
    try:
        options = app.json.loads(result.stdout)
    except ValueError:
        return jsonify({"error": "Could not parse event options.",
                        "stdout": result.stdout.strip()}), 500
    return jsonify(options)


@app.post("/api/roles/run")
def api_run_roles():
    payload = request.get_json(silent=True) or {}
    event_key = str(payload.get("event_key", "")).strip()
    before = str(payload.get("before", "")).strip()
    phase = str(payload.get("phase", "all")).strip() or "all"
    top = payload.get("top", 30)

    try:
        top_count = max(1, min(int(top), 200))
    except (TypeError, ValueError):
        top_count = 30

    if not event_key:
        return jsonify({"error": "event_key is required"}), 400
    if not BIN_PATH.exists():
        return jsonify({"error": "build/frc_prediction not found. Run cmake --build build first."}), 500

    args = [
        "--event", event_key,
        "--roles",
        "--json",
        "--top", str(top_count),
        "--phase", phase
    ]
    if before:
        args.extend(["--before", before])

    result = run_cli(args)
    if result.returncode != 0:
        return cli_error_response("Failed to compute team roles.", result)

    try:
        roles = app.json.loads(result.stdout)
    except ValueError:
        return jsonify({"error": "Could not parse roles output.",
                        "stdout": result.stdout.strip()}), 500
    return jsonify({"event_key": event_key, "phase": phase, "roles": roles})


@app.post("/api/alliance/run")
def api_run_alliance():
    payload = request.get_json(silent=True) or {}
    event_key = str(payload.get("event_key", "")).strip()
    alliance = str(payload.get("alliance", "")).strip()
    vs = str(payload.get("vs", "")).strip()

    if not event_key:
        return jsonify({"error": "event_key is required"}), 400
    if not alliance:
        return jsonify({"error": "alliance is required (e.g. frc254,frc1678,frc604)"}), 400
    if not BIN_PATH.exists():
        return jsonify({"error": "build/frc_prediction not found. Run cmake --build build first."}), 500

    args = [
        "--event", event_key,
        "--alliance", alliance,
        "--json"
    ]
    if vs:
        args.extend(["--vs", vs])

    result = run_cli(args)
    if result.returncode != 0:
        return cli_error_response("Failed to evaluate alliance.", result)

    try:
        evaluation = app.json.loads(result.stdout)
    except ValueError:
        return jsonify({"error": "Could not parse alliance output.",
                        "stdout": result.stdout.strip()}), 500
    return jsonify(evaluation)


def run_cli(args):
    return subprocess.run(
        [str(BIN_PATH), *args],
        cwd=BASE_DIR,
        capture_output=True,
        text=True,
        timeout=60,
        check=False
    )


def infer_stats_phase(match_key):
    """Pick the stats filter that matches how the CLI scores this match.

    Qualification matches use qualification-only stats; elimination matches use
    qualification plus played elimination matches. Mirrors normalize_match_key
    in the CLI so the dashboard table lines up with the prediction.
    """
    key = (match_key or "").strip().lower()
    if not key:
        return "qm"  # upcoming matches are typically qualification
    token = key.split("_")[-1]  # drop the event prefix if present
    if token.isdigit() or token.startswith("qm"):
        return "qm"
    if token.startswith(("qf", "sf", "f")):
        return "elim"
    return "qm"

def cli_error_response(message, result, hint=""):
    return jsonify({
        "error": message,
        "hint": hint,
        "stdout": result.stdout.strip(),
        "stderr": result.stderr.strip()
    }), 400


def read_stats_rows():
    with STATS_PATH.open(newline="") as stats_file:
        return list(csv.DictReader(stats_file))


def read_prediction_json():
    return app.json.loads(PREDICTION_PATH.read_text())


def collect_match_team_stats(event_key, phase, prediction, before=""):
    """Return per-team stats for the predicted match's teams.

    Uses a wide --stats-json pull so the match's teams are always available,
    even when they rank outside the dashboard table's Top N.
    """
    teams = list(prediction.get("red_teams", [])) + list(prediction.get("blue_teams", []))
    if not teams:
        return {}

    args = [
        "--event", event_key,
        "--stats-json",
        "--top", "1000",
        "--phase", phase
    ]
    if before:
        args.extend(["--before", before])
    result = run_cli(args)
    if result.returncode != 0:
        return {}

    try:
        rows = app.json.loads(result.stdout)
    except ValueError:
        return {}

    by_team = {row.get("team_key"): row for row in rows}
    return {team: by_team[team] for team in teams if team in by_team}

if __name__ == "__main__":
    app.run(host="127.0.0.1", port=5001, debug=True)
