#pragma once

#include <Arduino.h>

enum OutcomeType {
  OUTCOME_HOMERUN,
  OUTCOME_TRIPLE,
  OUTCOME_DOUBLE,
  OUTCOME_SINGLE,
  OUTCOME_FOUL,
  OUTCOME_BALL,
  OUTCOME_STRIKE,
  OUTCOME_OUT
};

struct PlayResult {
  String text;
  String image;
  OutcomeType outcome;
};

bool lookupPitchOutcome(
  const String &pitchHeight,
  const String &pitchSpeed,
  const String &swingHeight,
  const String &swingSpeed,
  PlayResult &result
);
