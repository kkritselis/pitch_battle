#include "pitch_outcomes.h"

#include <esp_random.h>

#include "pitch_outcomes_data.h"

static int pitchIndex3(const String &value, const char *a, const char *b, const char *c) {
  if (value == a) {
    return 0;
  }
  if (value == b) {
    return 1;
  }
  if (value == c) {
    return 2;
  }
  return -1;
}

static int pitchHeightIndex(const String &height) {
  return pitchIndex3(height, "high", "center", "low");
}

static int pitchSpeedIndex(const String &speed) {
  return pitchIndex3(speed, "fast", "medium", "slow");
}

static String normalizeHeight(const String &height) {
  if (height == "middle") {
    return "center";
  }
  return height;
}

static bool mapAction(PitchOutcomeAction action, PlayResult &result) {
  switch (action) {
    case PITCH_ACTION_HOMERUN:
      result.image = "homerun";
      result.outcome = OUTCOME_HOMERUN;
      return true;
    case PITCH_ACTION_TRIPLE:
      result.image = "triple";
      result.outcome = OUTCOME_TRIPLE;
      return true;
    case PITCH_ACTION_DOUBLE:
      result.image = "double";
      result.outcome = OUTCOME_DOUBLE;
      return true;
    case PITCH_ACTION_SINGLE:
      result.image = "single";
      result.outcome = OUTCOME_SINGLE;
      return true;
    case PITCH_ACTION_FOUL:
      result.image = "foul";
      result.outcome = OUTCOME_FOUL;
      return true;
    case PITCH_ACTION_BALL:
      result.image = "ball";
      result.outcome = OUTCOME_BALL;
      return true;
    case PITCH_ACTION_STRIKE:
      result.image = "strike";
      result.outcome = OUTCOME_STRIKE;
      return true;
    case PITCH_ACTION_FLYOUT:
      result.image = "flyout";
      result.outcome = OUTCOME_OUT;
      return true;
    case PITCH_ACTION_GROUNDOUT:
      result.image = "groundout";
      result.outcome = OUTCOME_OUT;
      return true;
    default:
      return false;
  }
}

static bool readProgmemString(const char *const *table, uint16_t index, String &dest) {
  if (index >= PITCH_OUTCOME_TEXT_COUNT) {
    return false;
  }

  char buffer[128];
  strcpy_P(buffer, (PGM_P)pgm_read_ptr(&table[index]));
  dest = buffer;
  return true;
}

bool lookupPitchOutcome(
  const String &pitchHeight,
  const String &pitchSpeed,
  const String &swingHeight,
  const String &swingSpeed,
  PlayResult &result
) {
  const String pitchH = normalizeHeight(pitchHeight);
  const String pitchS = pitchSpeed;
  const String swingH = normalizeHeight(swingHeight);
  const String swingS = swingSpeed;

  const int ph = pitchHeightIndex(pitchH);
  const int ps = pitchSpeedIndex(pitchS);
  const int sh = pitchHeightIndex(swingH);
  const int ss = pitchSpeedIndex(swingS);
  if (ph < 0 || ps < 0 || sh < 0 || ss < 0) {
    return false;
  }

  const size_t comboIndex = ((size_t)ph * 3U + (size_t)ps) * 9U + (size_t)sh * 3U + (size_t)ss;
  if (comboIndex >= PITCH_OUTCOME_COMBO_COUNT) {
    return false;
  }

  PitchOutcomeEntry entry;
  memcpy_P(&entry, &PITCH_OUTCOME_ENTRIES[comboIndex], sizeof(entry));
  if (entry.responseCount == 0) {
    return false;
  }

  const uint8_t roll = (uint8_t)(esp_random() % 100U);
  uint8_t cumulative = 0;

  for (uint8_t i = 0; i < entry.responseCount; i++) {
    PitchOutcomeResponse response;
    memcpy_P(
      &response,
      &PITCH_OUTCOME_RESPONSES[entry.responseStart + i],
      sizeof(response)
    );

    cumulative += response.weight;
    if (roll >= cumulative) {
      continue;
    }

    if (!readProgmemString(PITCH_OUTCOME_TEXTS, response.textIndex, result.text)) {
      return false;
    }
    return mapAction(response.action, result);
  }

  PitchOutcomeResponse fallback;
  memcpy_P(
    &fallback,
    &PITCH_OUTCOME_RESPONSES[entry.responseStart + entry.responseCount - 1],
    sizeof(fallback)
  );
  if (!readProgmemString(PITCH_OUTCOME_TEXTS, fallback.textIndex, result.text)) {
    return false;
  }
  return mapAction(fallback.action, result);
}
