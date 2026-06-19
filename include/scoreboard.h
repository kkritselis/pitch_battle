#pragma once

#include <Arduino.h>

constexpr uint8_t SCOREBOARD_WIDTH = 96;
constexpr uint8_t SCOREBOARD_HEIGHT = 16;
constexpr size_t SCOREBOARD_RGB_BYTES = SCOREBOARD_WIDTH * SCOREBOARD_HEIGHT * 3;
constexpr size_t SCOREBOARD_PNG_MAX_BYTES = 6200;

struct ScoreboardState {
  uint8_t inning = 1;
  bool topHalf = true;
  uint8_t homeScore = 0;
  uint8_t awayScore = 0;
  uint8_t balls = 0;
  uint8_t strikes = 0;
  uint8_t outs = 0;
  bool runnerFirst = false;
  bool runnerSecond = false;
  bool runnerThird = false;
};

size_t renderScoreboardPng(
  const ScoreboardState &state,
  uint8_t *out,
  size_t outCapacity
);
