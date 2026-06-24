const heights = ["high", "center", "low"];
const speeds = ["fast", "medium", "slow"];

const heightValue = {
  low: 0,
  center: 1,
  high: 2
};

const speedValue = {
  slow: 0,
  medium: 1,
  fast: 2
};

function getDirection(pitchSpeed, swingSpeed) {
  const diff = speedValue[swingSpeed] - speedValue[pitchSpeed];

  if (diff > 0) return "left";
  if (diff < 0) return "right";
  return "center";
}

function getTrajectory(pitchHeight, power) {
  if (pitchHeight === "low") return "ground";
  if (pitchHeight === "high") return "fly";

  if (power === "infield") return "ground";
  return "fly";
}

function getPowerScore(
  pitchHeight,
  swingHeight,
  pitchSpeed,
  swingSpeed
) {
  const heightScore =
    2 - Math.abs(heightValue[pitchHeight] - heightValue[swingHeight]);

  const speedScore =
    2 - Math.abs(speedValue[pitchSpeed] - speedValue[swingSpeed]);

  return heightScore + speedScore;
}

function getPowerLabel(score) {
  if (score <= 1) return "miss";
  if (score === 2) return "infield";
  if (score === 3) return "outfield";
  return "home_run";
}

function getHeightComment(pitchHeight, swingHeight) {
  const diff =
    heightValue[swingHeight] - heightValue[pitchHeight];

  if (diff === 0)
    return "He matched the height perfectly.";

  if (diff === 1)
    return "He got underneath the pitch.";

  if (diff === 2)
    return "He swung far underneath it.";

  if (diff === -1)
    return "He swung over the top of it.";

  return "He completely missed the plane of the pitch.";
}

function getTimingComment(pitchSpeed, swingSpeed) {
  const diff =
    speedValue[swingSpeed] - speedValue[pitchSpeed];

  switch (diff) {
    case 2:
      return "Way out in front.";
    case 1:
      return "A little early.";
    case 0:
      return "Perfect timing.";
    case -1:
      return "A little late.";
    case -2:
      return "Way behind the pitch.";
  }
}

function pitchDescription(height, speed) {
  return `That was a ${height} ${speed} pitch.`;
}

function generateRecord(
  pitchHeight,
  pitchSpeed,
  swingHeight,
  swingSpeed
) {
  const score = getPowerScore(
    pitchHeight,
    swingHeight,
    pitchSpeed,
    swingSpeed
  );

  const power = getPowerLabel(score);

  const direction =
    power === "miss"
      ? "none"
      : getDirection(pitchSpeed, swingSpeed);

  const trajectory =
    power === "miss"
      ? "none"
      : getTrajectory(pitchHeight, power);

  const commentary = [
    pitchDescription(pitchHeight, pitchSpeed),
    `The batter swung ${swingHeight} and ${swingSpeed}.`,
    getHeightComment(pitchHeight, swingHeight),
    getTimingComment(pitchSpeed, swingSpeed)
  ].join(" ");

  return {
    pitch: {
      height: pitchHeight,
      speed: pitchSpeed
    },
    swing: {
      height: swingHeight,
      speed: swingSpeed
    },
    commentary,
    result: {
      direction,
      trajectory,
      power
    }
  };
}

const results = [];

for (const pitchHeight of heights) {
  for (const pitchSpeed of speeds) {
    for (const swingHeight of heights) {
      for (const swingSpeed of speeds) {
        results.push(
          generateRecord(
            pitchHeight,
            pitchSpeed,
            swingHeight,
            swingSpeed
          )
        );
      }
    }
  }
}

console.log(JSON.stringify(results, null, 2));