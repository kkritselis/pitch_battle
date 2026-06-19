#include "scoreboard.h"

struct Rgb {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

static constexpr Rgb COLOR_BLACK = {0, 0, 0};
static constexpr Rgb COLOR_WHITE = {220, 235, 255};
static constexpr Rgb COLOR_BLUE = {0, 70, 170};
static constexpr Rgb COLOR_DIM = {45, 55, 70};
static constexpr Rgb COLOR_YELLOW = {255, 190, 35};
static constexpr Rgb COLOR_CYAN = {40, 210, 255};
static constexpr Rgb COLOR_GREEN = {60, 255, 85};

static uint8_t pixelBuffer[SCOREBOARD_RGB_BYTES];
static uint8_t pngRawBuffer[SCOREBOARD_HEIGHT * (1 + SCOREBOARD_WIDTH * 3)];

static const uint8_t GLYPH_SPACE[5] = {
  0b000,
  0b000,
  0b000,
  0b000,
  0b000
};

static const uint8_t *glyphFor(char c) {
  static const uint8_t digits[10][5] = {
    {0b111, 0b101, 0b101, 0b101, 0b111},
    {0b010, 0b110, 0b010, 0b010, 0b111},
    {0b111, 0b001, 0b111, 0b100, 0b111},
    {0b111, 0b001, 0b111, 0b001, 0b111},
    {0b101, 0b101, 0b111, 0b001, 0b001},
    {0b111, 0b100, 0b111, 0b001, 0b111},
    {0b111, 0b100, 0b111, 0b101, 0b111},
    {0b111, 0b001, 0b010, 0b010, 0b010},
    {0b111, 0b101, 0b111, 0b101, 0b111},
    {0b111, 0b101, 0b111, 0b001, 0b111}
  };

  static const uint8_t glyphA[5] = {0b010, 0b101, 0b111, 0b101, 0b101};
  static const uint8_t glyphB[5] = {0b110, 0b101, 0b110, 0b101, 0b110};
  static const uint8_t glyphH[5] = {0b101, 0b101, 0b111, 0b101, 0b101};
  static const uint8_t glyphO[5] = {0b111, 0b101, 0b101, 0b101, 0b111};
  static const uint8_t glyphS[5] = {0b111, 0b100, 0b111, 0b001, 0b111};
  static const uint8_t glyphT[5] = {0b111, 0b010, 0b010, 0b010, 0b010};
  static const uint8_t glyphDash[5] = {0b000, 0b000, 0b111, 0b000, 0b000};

  if (c >= '0' && c <= '9') return digits[c - '0'];
  if (c == 'A') return glyphA;
  if (c == 'B') return glyphB;
  if (c == 'H') return glyphH;
  if (c == 'O') return glyphO;
  if (c == 'S') return glyphS;
  if (c == 'T') return glyphT;
  if (c == '-') return glyphDash;
  return GLYPH_SPACE;
}

static void putPixel(uint8_t x, uint8_t y, Rgb color) {
  if (x >= SCOREBOARD_WIDTH || y >= SCOREBOARD_HEIGHT) {
    return;
  }

  const size_t offset = ((size_t)y * SCOREBOARD_WIDTH + x) * 3;
  pixelBuffer[offset] = color.r;
  pixelBuffer[offset + 1] = color.g;
  pixelBuffer[offset + 2] = color.b;
}

static void drawChar(uint8_t x, uint8_t y, char c, Rgb color) {
  const uint8_t *glyph = glyphFor(c);
  for (uint8_t row = 0; row < 5; row++) {
    for (uint8_t col = 0; col < 3; col++) {
      if ((glyph[row] & (0b100 >> col)) != 0) {
        putPixel(x + col, y + row, color);
      }
    }
  }
}

static uint8_t drawText(uint8_t x, uint8_t y, const char *text, Rgb color) {
  while (*text != '\0' && x < SCOREBOARD_WIDTH) {
    drawChar(x, y, *text, color);
    x += 4;
    text++;
  }
  return x;
}

static uint8_t appendNumber(char *out, uint8_t value) {
  if (value >= 100) {
    out[0] = '9';
    out[1] = '9';
    return 2;
  }

  if (value >= 10) {
    out[0] = '0' + value / 10;
    out[1] = '0' + value % 10;
    return 2;
  }

  out[0] = '0' + value;
  return 1;
}

static void buildScoreText(const ScoreboardState &state, char *out, size_t outSize) {
  if (outSize < 16) {
    if (outSize > 0) out[0] = '\0';
    return;
  }

  uint8_t pos = 0;
  out[pos++] = 'A';
  pos += appendNumber(out + pos, state.awayScore);
  out[pos++] = ' ';
  out[pos++] = 'H';
  pos += appendNumber(out + pos, state.homeScore);
  out[pos++] = ' ';
  out[pos++] = state.topHalf ? 'T' : 'B';
  pos += appendNumber(out + pos, state.inning);
  out[pos] = '\0';
}

static void buildCountText(const ScoreboardState &state, char *out, size_t outSize) {
  if (outSize < 14) {
    if (outSize > 0) out[0] = '\0';
    return;
  }

  uint8_t pos = 0;
  out[pos++] = 'B';
  pos += appendNumber(out + pos, state.balls);
  out[pos++] = ' ';
  out[pos++] = 'S';
  pos += appendNumber(out + pos, state.strikes);
  out[pos++] = ' ';
  out[pos++] = 'O';
  pos += appendNumber(out + pos, state.outs);
  out[pos] = '\0';
}

static void fillBackground() {
  for (uint8_t y = 0; y < SCOREBOARD_HEIGHT; y++) {
    for (uint8_t x = 0; x < SCOREBOARD_WIDTH; x++) {
      putPixel(x, y, COLOR_BLACK);
    }
  }

  for (uint8_t x = 0; x < SCOREBOARD_WIDTH; x++) {
    putPixel(x, 7, COLOR_BLUE);
  }
}

static void drawBaseMarker(uint8_t cx, uint8_t cy, bool occupied) {
  const Rgb color = occupied ? COLOR_GREEN : COLOR_DIM;
  putPixel(cx, cy - 1, color);
  putPixel(cx - 1, cy, color);
  putPixel(cx, cy, color);
  putPixel(cx + 1, cy, color);
  putPixel(cx, cy + 1, color);
}

static void renderFramebuffer(const ScoreboardState &state) {
  fillBackground();

  char score[16];
  char count[14];
  buildScoreText(state, score, sizeof(score));
  buildCountText(state, count, sizeof(count));

  uint8_t x = drawText(1, 1, score, COLOR_YELLOW);
  while (x < 60) {
    x += 4;
  }
  drawText(x, 1, state.topHalf ? "T" : "B", COLOR_WHITE);

  drawText(1, 9, count, COLOR_CYAN);
  drawBaseMarker(84, 12, state.runnerThird);
  drawBaseMarker(89, 10, state.runnerSecond);
  drawBaseMarker(94, 12, state.runnerFirst);
}

static uint32_t crc32Update(uint32_t crc, const uint8_t *data, size_t length) {
  crc = ~crc;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1)));
    }
  }
  return ~crc;
}

static uint32_t adler32(const uint8_t *data, size_t length) {
  uint32_t a = 1;
  uint32_t b = 0;

  for (size_t i = 0; i < length; i++) {
    a = (a + data[i]) % 65521;
    b = (b + a) % 65521;
  }

  return (b << 16) | a;
}

static bool writeByte(uint8_t *out, size_t capacity, size_t &pos, uint8_t value) {
  if (pos >= capacity) {
    return false;
  }

  out[pos++] = value;
  return true;
}

static bool writeBytes(
  uint8_t *out,
  size_t capacity,
  size_t &pos,
  const uint8_t *data,
  size_t length
) {
  if (pos + length > capacity) {
    return false;
  }

  if (length > 0) {
    memcpy(out + pos, data, length);
  }
  pos += length;
  return true;
}

static bool writeBE32(uint8_t *out, size_t capacity, size_t &pos, uint32_t value) {
  return writeByte(out, capacity, pos, (value >> 24) & 0xFF)
    && writeByte(out, capacity, pos, (value >> 16) & 0xFF)
    && writeByte(out, capacity, pos, (value >> 8) & 0xFF)
    && writeByte(out, capacity, pos, value & 0xFF);
}

static bool writeChunk(
  uint8_t *out,
  size_t capacity,
  size_t &pos,
  const char type[4],
  const uint8_t *data,
  size_t length
) {
  if (!writeBE32(out, capacity, pos, length)) {
    return false;
  }

  const size_t crcStart = pos;
  if (!writeBytes(out, capacity, pos, (const uint8_t *)type, 4)) {
    return false;
  }
  if (!writeBytes(out, capacity, pos, data, length)) {
    return false;
  }

  const uint32_t crc = crc32Update(0, out + crcStart, 4 + length);
  return writeBE32(out, capacity, pos, crc);
}

static size_t buildZlibStoredBlock(
  uint8_t *out,
  size_t capacity,
  const uint8_t *data,
  size_t length
) {
  if (length > 65535 || capacity < length + 11) {
    return 0;
  }

  size_t pos = 0;
  out[pos++] = 0x78;
  out[pos++] = 0x01;
  out[pos++] = 0x01;
  out[pos++] = length & 0xFF;
  out[pos++] = (length >> 8) & 0xFF;
  const uint16_t nlen = ~((uint16_t)length);
  out[pos++] = nlen & 0xFF;
  out[pos++] = (nlen >> 8) & 0xFF;
  memcpy(out + pos, data, length);
  pos += length;

  const uint32_t adler = adler32(data, length);
  out[pos++] = (adler >> 24) & 0xFF;
  out[pos++] = (adler >> 16) & 0xFF;
  out[pos++] = (adler >> 8) & 0xFF;
  out[pos++] = adler & 0xFF;
  return pos;
}

size_t renderScoreboardPng(
  const ScoreboardState &state,
  uint8_t *out,
  size_t outCapacity
) {
  if (out == nullptr || outCapacity == 0) {
    return 0;
  }

  renderFramebuffer(state);

  size_t rawPos = 0;
  for (uint8_t y = 0; y < SCOREBOARD_HEIGHT; y++) {
    pngRawBuffer[rawPos++] = 0x00;
    memcpy(
      pngRawBuffer + rawPos,
      pixelBuffer + ((size_t)y * SCOREBOARD_WIDTH * 3),
      SCOREBOARD_WIDTH * 3
    );
    rawPos += SCOREBOARD_WIDTH * 3;
  }

  static uint8_t idatBuffer[sizeof(pngRawBuffer) + 11];
  const size_t idatLength =
    buildZlibStoredBlock(idatBuffer, sizeof(idatBuffer), pngRawBuffer, rawPos);
  if (idatLength == 0) {
    return 0;
  }

  size_t pos = 0;
  static const uint8_t signature[] = {
    0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A
  };
  if (!writeBytes(out, outCapacity, pos, signature, sizeof(signature))) {
    return 0;
  }

  uint8_t ihdr[13] = {
    0, 0, 0, SCOREBOARD_WIDTH,
    0, 0, 0, SCOREBOARD_HEIGHT,
    8, 2, 0, 0, 0
  };

  if (!writeChunk(out, outCapacity, pos, "IHDR", ihdr, sizeof(ihdr))) {
    return 0;
  }
  if (!writeChunk(out, outCapacity, pos, "IDAT", idatBuffer, idatLength)) {
    return 0;
  }
  if (!writeChunk(out, outCapacity, pos, "IEND", nullptr, 0)) {
    return 0;
  }

  return pos;
}
