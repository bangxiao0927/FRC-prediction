"""Tests for the Flask dashboard endpoints.

The web app shells out to the compiled CLI; these tests mock ``run_cli`` (and the
file/IO helpers) so they run fast and offline, exercising request validation,
response shaping, and that the right CLI flags are assembled.
"""
import json
import types

import pytest

import app as flask_app


def fake_proc(stdout="", returncode=0, stderr=""):
    return types.SimpleNamespace(returncode=returncode, stdout=stdout, stderr=stderr)


@pytest.fixture
def client(monkeypatch, tmp_path):
    # Pretend the compiled binary exists so endpoints get past the guard.
    binary = tmp_path / "frc_prediction"
    binary.write_text("")
    monkeypatch.setattr(flask_app, "BIN_PATH", binary)
    flask_app.load_events_for_year.cache_clear()
    flask_app.app.config.update(TESTING=True)
    return flask_app.app.test_client()


def test_events_returns_parsed_payload(client, monkeypatch):
    payload = {"year": 2024, "events": [
        {"key": "2024casj", "name": "Silicon Valley Regional", "start_date": "2024-02-29", "week": -1}
    ]}
    monkeypatch.setattr(flask_app, "run_cli", lambda args: fake_proc(json.dumps(payload)))
    response = client.post("/api/events", json={"year": 2024})
    assert response.status_code == 200
    assert response.get_json()["events"][0]["key"] == "2024casj"


def test_events_rejects_bad_year(client):
    assert client.post("/api/events", json={"year": 0}).status_code == 400
    assert client.post("/api/events", json={"year": "abc"}).status_code == 400


def test_events_search_returns_cross_year_matches(client, monkeypatch):
    payloads = {
        2024: {"year": 2024, "events": [
            {"key": "2024casj", "name": "Silicon Valley Regional"},
            {"key": "2024cabl", "name": "Central Valley Regional"},
        ]},
        2023: {"year": 2023, "events": [
            {"key": "2023txcmp", "name": "Texas Championship"}
        ]},
    }

    def fake_run(args):
        year = int(args[1])
        return fake_proc(json.dumps(payloads.get(year, {"year": year, "events": []})))

    monkeypatch.setattr(flask_app, "run_cli", fake_run)
    monkeypatch.setattr(flask_app, "search_years", lambda hint=None: [2024, 2023])

    response = client.post("/api/events/search", json={"query": "tex champ", "year_hint": 2024, "limit": 5})
    body = response.get_json()
    assert response.status_code == 200
    assert body["events"][0]["key"] == "2023txcmp"
    assert body["events"][0]["year"] == 2023


def test_events_search_requires_query(client):
    assert client.post("/api/events/search", json={}).status_code == 400


def test_event_options_returns_teams_and_matches(client, monkeypatch):
    payload = {
        "event_key": "2024casj",
        "teams": [{"key": "frc254", "team_number": 254, "nickname": "The Cheesy Poofs"}],
        "matches": [{"key": "2024casj_qm1", "label": "Qual 1", "comp_level": "qm", "played": True}],
    }
    monkeypatch.setattr(flask_app, "run_cli", lambda args: fake_proc(json.dumps(payload)))
    response = client.post("/api/event/options", json={"event_key": "2024casj"})
    body = response.get_json()
    assert response.status_code == 200
    assert body["teams"][0]["key"] == "frc254"
    assert body["matches"][0]["key"] == "2024casj_qm1"


def test_event_options_requires_event_key(client):
    assert client.post("/api/event/options", json={}).status_code == 400


def test_roles_wraps_cli_output(client, monkeypatch):
    roles = [{"team_key": "frc254", "primary_role": "offense", "offense": 40.0}]
    captured = {}

    def fake_run(args):
        captured["args"] = args
        return fake_proc(json.dumps(roles))

    monkeypatch.setattr(flask_app, "run_cli", fake_run)
    response = client.post("/api/roles/run", json={"event_key": "2024casj", "phase": "qm", "top": 5})
    body = response.get_json()
    assert response.status_code == 200
    assert body["event_key"] == "2024casj"
    assert body["roles"] == roles
    assert "--roles" in captured["args"] and "--phase" in captured["args"]


def test_alliance_passes_through(client, monkeypatch):
    evaluation = {"event_key": "2024casj", "alliance": {"predicted_score": 91.6}}
    captured = {}

    def fake_run(args):
        captured["args"] = args
        return fake_proc(json.dumps(evaluation))

    monkeypatch.setattr(flask_app, "run_cli", fake_run)
    response = client.post("/api/alliance/run", json={"event_key": "2024casj", "alliance": "frc254,frc1678", "vs": "frc8"})
    assert response.status_code == 200
    assert response.get_json()["alliance"]["predicted_score"] == 91.6
    assert "--alliance" in captured["args"] and "--vs" in captured["args"]


def test_alliance_requires_lineup(client):
    assert client.post("/api/alliance/run", json={"event_key": "2024casj"}).status_code == 400


def test_prediction_forwards_history_flags(client, monkeypatch):
    calls = []

    def fake_run(args):
        calls.append(args)
        return fake_proc("{}")

    monkeypatch.setattr(flask_app, "run_cli", fake_run)
    # Avoid real file IO for the response-shaping helpers.
    monkeypatch.setattr(flask_app, "read_stats_rows", lambda: [])
    monkeypatch.setattr(flask_app, "read_prediction_json", lambda: {"red_teams": [], "blue_teams": []})
    monkeypatch.setattr(flask_app, "collect_match_team_stats", lambda *a, **k: {})

    response = client.post("/api/prediction/run", json={
        "event_key": "2024cacc", "match_key": "qm5", "history_teams": "frc114"
    })
    assert response.status_code == 200
    flat = [token for call in calls for token in call]
    assert "--history-teams" in flat and "frc114" in flat


def test_prediction_requires_event(client):
    assert client.post("/api/prediction/run", json={}).status_code == 400


def test_missing_binary_returns_500(client, monkeypatch, tmp_path):
    monkeypatch.setattr(flask_app, "BIN_PATH", tmp_path / "does_not_exist")
    assert client.post("/api/events", json={"year": 2024}).status_code == 500
    assert client.post("/api/event/options", json={"event_key": "2024casj"}).status_code == 500
