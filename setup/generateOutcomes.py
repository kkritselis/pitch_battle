#!/usr/bin/env python3
"""Compile setup/pitching-battle-outcomes.json into a flash lookup table."""

from __future__ import annotations

import json
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]
SOURCE_PATH = Path(__file__).resolve().parent / "pitching-battle-outcomes.json"
OUTPUT_PATH = PROJECT_ROOT / "include" / "pitch_outcomes_data.h"

HEIGHTS = ["high", "center", "low"]
SPEEDS = ["fast", "medium", "slow"]

ACTIONS = {
    "homerun": "PITCH_ACTION_HOMERUN",
    "triple": "PITCH_ACTION_TRIPLE",
    "double": "PITCH_ACTION_DOUBLE",
    "single": "PITCH_ACTION_SINGLE",
    "foul": "PITCH_ACTION_FOUL",
    "ball": "PITCH_ACTION_BALL",
    "strike": "PITCH_ACTION_STRIKE",
    "flyout": "PITCH_ACTION_FLYOUT",
    "groundout": "PITCH_ACTION_GROUNDOUT",
}

# Injected into play-only combos (weights must sum to 100 with PLAY_WEIGHT_BUDGET).
EXTRA_STRIKE_WEIGHT = 20
EXTRA_BALL_WEIGHT = 10
PLAY_WEIGHT_BUDGET = 100 - EXTRA_STRIKE_WEIGHT - EXTRA_BALL_WEIGHT

EXTRA_STRIKE_RESPONSES = [
    {
        "weight": 4,
        "text": "Swing and a miss!",
        "action": "strike",
    },
    {
        "weight": 3,
        "text": "He chased that one and came up empty.",
        "action": "strike",
    },
    {
        "weight": 3,
        "text": "The bat never touched it. Strike!",
        "action": "strike",
    },
]

EXTRA_BALL_RESPONSES = [
    {
        "weight": 3,
        "text": "Ball, outside the zone.",
        "action": "ball",
    },
    {
        "weight": 2,
        "text": "Good eye. He takes it for a ball.",
        "action": "ball",
    },
]


def scale_weights(items: list[dict], target: int) -> list[dict]:
    total = sum(item["weight"] for item in items)
    if total <= 0:
        return items

    scaled: list[dict] = []
    running = 0
    for index, item in enumerate(items):
        if index == len(items) - 1:
            weight = target - running
        else:
            weight = round(item["weight"] * target / total)
            running += weight
        scaled.append({**item, "weight": weight})
    return scaled


def expand_responses(responses: list[dict]) -> list[dict]:
    play = [item for item in responses if item["action"] not in ("strike", "ball")]
    if not play:
        return responses

    expanded = scale_weights(play, PLAY_WEIGHT_BUDGET)
    expanded.extend(EXTRA_STRIKE_RESPONSES)
    expanded.extend(EXTRA_BALL_RESPONSES)

    total_weight = sum(item["weight"] for item in expanded)
    if total_weight != 100:
        raise SystemExit(
            f"Expanded combo weights sum to {total_weight}, expected 100 "
            f"(play={PLAY_WEIGHT_BUDGET}, strike={EXTRA_STRIKE_WEIGHT}, ball={EXTRA_BALL_WEIGHT})"
        )
    return expanded


def combo_index(pitch_height: str, pitch_speed: str, swing_height: str, swing_speed: str) -> int:
    ph = HEIGHTS.index(pitch_height)
    ps = SPEEDS.index(pitch_speed)
    sh = HEIGHTS.index(swing_height)
    ss = SPEEDS.index(swing_speed)
    return ((ph * 3 + ps) * 9) + (sh * 3 + ss)


def c_escape(text: str) -> str:
    return (
        text.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\n", "\\n")
    )


def main() -> None:
    if not SOURCE_PATH.is_file():
        raise SystemExit(f"Missing {SOURCE_PATH}")

    rows = json.loads(SOURCE_PATH.read_text())
    by_index: dict[int, dict] = {}

    for row in rows:
        pitch = row["pitch"]
        swing = row["swing"]
        idx = combo_index(pitch["height"], pitch["speed"], swing["height"], swing["speed"])
        if idx in by_index:
            raise SystemExit(f"Duplicate combo index {idx}: {row}")
        by_index[idx] = row

    missing = [i for i in range(81) if i not in by_index]
    if missing:
        raise SystemExit(f"Missing {len(missing)} combo(s), first={missing[:5]}")

    string_pool: list[str] = []
    string_index: dict[str, int] = {}

    def intern(text: str) -> int:
        if text not in string_index:
            string_index[text] = len(string_pool)
            string_pool.append(text)
        return string_index[text]

    flat_responses: list[dict] = []
    entry_meta: list[tuple[int, int]] = []

    for idx in range(81):
        responses = expand_responses(by_index[idx]["result"]["responses"])
        total_weight = sum(item["weight"] for item in responses)
        if total_weight != 100:
            raise SystemExit(f"Combo {idx} weights sum to {total_weight}, expected 100")

        start = len(flat_responses)
        for item in responses:
            action = item["action"]
            if action not in ACTIONS:
                raise SystemExit(f"Unknown action '{action}' in combo {idx}")
            flat_responses.append(
                {
                    "weight": item["weight"],
                    "action": action,
                    "text_id": intern(item["text"]),
                }
            )
        entry_meta.append((start, len(responses)))

    lines = [
        "#pragma once",
        "",
        "#include <Arduino.h>",
        "",
        "// Generated from setup/pitching-battle-outcomes.json",
        "// Regenerate with: python3 setup/generateOutcomes.py",
        "",
        "enum PitchOutcomeAction : uint8_t {",
        "  PITCH_ACTION_HOMERUN,",
        "  PITCH_ACTION_TRIPLE,",
        "  PITCH_ACTION_DOUBLE,",
        "  PITCH_ACTION_SINGLE,",
        "  PITCH_ACTION_FOUL,",
        "  PITCH_ACTION_BALL,",
        "  PITCH_ACTION_STRIKE,",
        "  PITCH_ACTION_FLYOUT,",
        "  PITCH_ACTION_GROUNDOUT,",
        "};",
        "",
        "struct PitchOutcomeResponse {",
        "  uint8_t weight;",
        "  PitchOutcomeAction action;",
        "  uint16_t textIndex;",
        "};",
        "",
        "struct PitchOutcomeEntry {",
        "  uint16_t responseStart;",
        "  uint8_t responseCount;",
        "};",
        "",
        f"constexpr size_t PITCH_OUTCOME_COMBO_COUNT = {len(entry_meta)};",
        f"constexpr size_t PITCH_OUTCOME_RESPONSE_COUNT = {len(flat_responses)};",
        f"constexpr size_t PITCH_OUTCOME_TEXT_COUNT = {len(string_pool)};",
        "",
        "const char* const PITCH_OUTCOME_TEXTS[PITCH_OUTCOME_TEXT_COUNT] PROGMEM = {",
    ]

    for text in string_pool:
        lines.append(f'  "{c_escape(text)}",')
    lines.append("};")
    lines.append("")

    lines.append(
        "const PitchOutcomeResponse "
        "PITCH_OUTCOME_RESPONSES[PITCH_OUTCOME_RESPONSE_COUNT] PROGMEM = {"
    )
    for item in flat_responses:
        lines.append(
            "  { "
            f"{item['weight']}, "
            f"{ACTIONS[item['action']]}, "
            f"{item['text_id']} "
            "},"
        )
    lines.append("};")
    lines.append("")

    lines.append(
        "const PitchOutcomeEntry "
        "PITCH_OUTCOME_ENTRIES[PITCH_OUTCOME_COMBO_COUNT] PROGMEM = {"
    )
    for start, count in entry_meta:
        lines.append(f"  {{ {start}, {count} }},")
    lines.append("};")
    lines.append("")

    OUTPUT_PATH.write_text("\n".join(lines))
    print(
        f"Wrote {OUTPUT_PATH} "
        f"({len(entry_meta)} combos, {len(flat_responses)} responses, "
        f"{len(string_pool)} unique texts)"
    )


if __name__ == "__main__":
    main()
