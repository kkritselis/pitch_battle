const fs = require("fs");

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
  if (power === "miss") return "none";
  if (pitchHeight === "low") return "ground";
  if (pitchHeight === "high") return "fly";

  return power === "infield" ? "ground" : "fly";
}

function getPowerScore(pitchHeight, swingHeight, pitchSpeed, swingSpeed) {
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

function formatPitch(height, speed) {
  return `${height} ${speed} pitch`;
}

function getHomeRunResponses(pitchHeight, pitchSpeed) {
  const pitch = formatPitch(pitchHeight, pitchSpeed);

  return [
    {
      weight: 25,
      text: `That was a ${pitch}. The batter matched it perfectly. That ball is crushed! Way back and gone! Home run!`,
      action: "homerun"
    },
    {
      weight: 25,
      text: `That was a ${pitch} and the batter got every bit of that one. Home run!`,
      action: "homerun"
    },
    {
      weight: 25,
      text: `That was a ${pitch}. No doubt about it. That one is headed for the bleachers! Home run!`,
      action: "homerun"
    },
    {
      weight: 25,
      text: `That was a ${pitch}. A towering drive to deep center field and it is out of here! Perfect timing. Home run!`,
      action: "homerun"
    }
  ];
}

function getMissResponses(pitchHeight, pitchSpeed, swingHeight, swingSpeed) {
    const pitch = formatPitch(pitchHeight, pitchSpeed);
  
    return [
      // STRIKES
  
      {
        weight: 10,
        text: `That was a ${pitch}. The batter swung ${swingHeight} and ${swingSpeed}, but missed it completely.`,
        action: "strike"
      },
      {
        weight: 10,
        text: `The pitcher wins that battle. A ${pitch} gets past the batter for a strike.`,
        action: "strike"
      },
      {
        weight: 10,
        text: `Swing and a miss! The ${pitch} was too much for him.`,
        action: "strike"
      },
      {
        weight: 10,
        text: `He chased a ${pitch} and came up empty.`,
        action: "strike"
      },
      {
        weight: 10,
        text: `The batter offered at the ${pitch} and missed for strike one.`,
        action: "strike"
      },
  
      // BALLS
  
      {
        weight: 10,
        text: `The batter checks his swing and lets the ${pitch} go by. Ball!`,
        action: "ball"
      },
      {
        weight: 10,
        text: `He holds up and watches the ${pitch} miss the zone. Ball.`,
        action: "ball"
      },
      {
        weight: 10,
        text: `Good eye by the batter. He lays off the ${pitch} for a ball.`,
        action: "ball"
      },
      {
        weight: 10,
        text: `The batter isn't fooled. He takes the ${pitch} and the umpire calls ball.`,
        action: "ball"
      },
      {
        weight: 10,
        text: `The ${pitch} stays outside and the batter wisely lets it pass. Ball.`,
        action: "ball"
      }
    ];
  }

function getCenterResponses(trajectory, power) {
  if (power === "infield") {
    return [
      {
        weight: 60,
        text: "Right back up the middle, but the defense handles it cleanly.",
        action: "ground_out"
      },
      {
        weight: 40,
        text: "A chopper up the middle sneaks through for a base hit.",
        action: "single"
      }
    ];
  }

  return [
    {
      weight: 50,
      text: "Hit right back up the middle, but the defense tracks it down.",
      action: trajectory === "fly" ? "fly_out" : "out"
    },
    {
      weight: 35,
      text: "Line drive up the middle for a base hit.",
      action: "single"
    },
    {
      weight: 10,
      text: "Driven deep into center field for extra bases.",
      action: "double"
    },
    {
      weight: 5,
      text: "That ball rolls all the way to the wall in center.",
      action: "triple"
    }
  ];
}

function getSideResponses(direction, trajectory, power) {
  const side = direction === "left" ? "left" : "right";
  const corner = direction === "left" ? "left field corner" : "right field corner";

  if (power === "infield") {
    return [
      {
        weight: 20,
        text: `He sends it foul down the ${side} side.`,
        action: "foul"
      },
      {
        weight: 40,
        text: `Grounded to the ${side} side. Easy play at first.`,
        action: "ground_out"
      },
      {
        weight: 40,
        text: `A sharp grounder through the ${side} side for a base hit.`,
        action: "single"
      }
    ];
  }

  return [
    {
      weight: 20,
      text: `He sends it foul down the ${side} line.`,
      action: "foul"
    },
    {
      weight: 20,
      text: `A deep pop up to ${side}. The fielder makes the catch.`,
      action: "fly_out"
    },
    {
      weight: 20,
      text: `Grounded to the ${side} side. Easy play at first.`,
      action: "ground_out"
    },
    {
      weight: 25,
      text: `Line drive into ${side} field for a base hit.`,
      action: "single"
    },
    {
      weight: 10,
      text: `That one gets past the outfielder. He is headed for second.`,
      action: "double"
    },
    {
      weight: 5,
      text: `Deep into the ${corner}. He is flying around the bases.`,
      action: "triple"
    }
  ];
}

function getResponses({
  pitchHeight,
  pitchSpeed,
  swingHeight,
  swingSpeed,
  direction,
  trajectory,
  power
}) {
  if (power === "home_run") {
    return getHomeRunResponses(pitchHeight, pitchSpeed);
  }

  if (power === "miss") {
    return getMissResponses(pitchHeight, pitchSpeed, swingHeight, swingSpeed);
  }

  if (direction === "center") {
    return getCenterResponses(trajectory, power);
  }

  return getSideResponses(direction, trajectory, power);
}

function generateRecord(pitchHeight, pitchSpeed, swingHeight, swingSpeed) {
  const score = getPowerScore(
    pitchHeight,
    swingHeight,
    pitchSpeed,
    swingSpeed
  );

  const power = getPowerLabel(score);

  const direction =
    power === "miss" ? "none" : getDirection(pitchSpeed, swingSpeed);

  const trajectory = getTrajectory(pitchHeight, power);

  const responses = getResponses({
    pitchHeight,
    pitchSpeed,
    swingHeight,
    swingSpeed,
    direction,
    trajectory,
    power
  });

  return {
    pitch: {
      height: pitchHeight,
      speed: pitchSpeed
    },
    swing: {
      height: swingHeight,
      speed: swingSpeed
    },
    result: {
      direction,
      trajectory,
      power,
      responses
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

fs.writeFileSync(
  "pitching-battle-outcomes.json",
  JSON.stringify(results, null, 2)
);

console.log(`Generated ${results.length} outcomes.`);