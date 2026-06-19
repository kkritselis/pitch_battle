#include "scoreboard.h"
#include "esp32c3/rom/miniz.h"

struct Rgb {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

static constexpr Rgb COLOR_BLACK = {0, 0, 0};
static constexpr Rgb COLOR_WHITE = {220, 235, 255};
static constexpr Rgb COLOR_YELLOW = {255, 190, 35};
static constexpr Rgb COLOR_CYAN = {40, 210, 255};
static constexpr Rgb COLOR_RED = {255, 0, 0};

static uint8_t pixelBuffer[SCOREBOARD_RGB_BYTES];
static uint8_t pngRawBuffer[SCOREBOARD_HEIGHT * (1 + SCOREBOARD_WIDTH * 3)];

static const char *const SCOREBOARD_TEMPLATE[SCOREBOARD_HEIGHT] = {
  ".....CC...C..C...C....CC......................WWWWWWWWWWWWWWWWWWWwWWWWWwWWWWWwWWWWWW#WWWWWWWWWW#",
  ".....C.C.C.C.C...C...C...............##.......WW.................w.....w.....w.....W#W........W#",
  ".....CC..CCC.C...C....C..............##.......WW.c.c.c..cc.c.ccc.w.....w.....w.....W#W........W#",
  ".....C.C.C.C.C...C.....C............G..G......WW.c.c.c.c...c..c..w.....w.....w.....W#W........W#",
  ".....CC..C.C.CCC.CCC.CC............G....G.....WW.c.c.c..c..c..c..w.....w.....w.....W#W........W#",
  "......YY.YYY.YY..Y.Y.Y.YYY..YY....G......G....WW..c..c...c.c..c..w.....w.....w.....W#W........W#",
  ".....Y....Y..Y.Y.Y.Y.Y.Y...Y.....G........G...WW..c..c.cc..c..c..w.....w.....w.....W#W........W#",
  "......Y...Y..YY..Y.YY..YY...Y..W#..........##.Wwwwwwwwwwwwwwwwwwwpwwwwwpwwwwwpwwwwww#wwwwwwwwww#",
  ".......Y..Y..Y.Y.Y.Y.Y.Y.....Y.W#....##....##.WW.................w.....w.....w.....W#W........W#",
  ".....YY...Y..Y.Y.Y.Y.Y.YYY.YY....G........G...WW.Y.Y..Y..Y.Y.YYY.w.....w.....w.....W#W........W#",
  "......C..C.C.CCC..CC..............G......G....WW.Y.Y.Y.Y.YYY.Y...w.....w.....w.....W#W........W#",
  ".....C.C.C.C..C..C.................G....G.....WW.YYY.Y.Y.YYY.YY..w.....w.....w.....W#W........W#",
  ".....C.C.C.C..C...C.................G..G......WW.Y.Y.Y.Y.Y.Y.Y...w.....w.....w.....W#W........W#",
  ".....C.C.C.C..C....C.................##.......WW.Y.Y..Y..Y.Y.YYY.w.....w.....w.....W#W........W#",
  "......C...C...C..CC..................##.......WWWwWwWWWWWwWwWW#WWwWWWWWwWWWWWwWWWWWW#WWWWWWWWWW#",
  "..............................................W#################################################"
};

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

static Rgb templateColor(char c) {
  switch (c) {
    case 'C': return {38, 188, 200};
    case 'c': return {24, 136, 145};
    case 'W': return {251, 251, 251};
    case 'w': return {247, 247, 247};
    case '#': return {255, 255, 255};
    case 'p': return {239, 239, 239};
    case 'Y': return {225, 246, 47};
    case 'G': return {0, 255, 18};
    default: return COLOR_BLACK;
  }
}

static void drawTemplate() {
  for (uint8_t y = 0; y < SCOREBOARD_HEIGHT; y++) {
    for (uint8_t x = 0; x < SCOREBOARD_WIDTH; x++) {
      putPixel(x, y, templateColor(SCOREBOARD_TEMPLATE[y][x]));
    }
  }
}

static void fillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, Rgb color) {
  for (uint8_t yy = 0; yy < h; yy++) {
    for (uint8_t xx = 0; xx < w; xx++) {
      putPixel(x + xx, y + yy, color);
    }
  }
}

static void drawBaseBox(uint8_t x, uint8_t y, bool occupied) {
  fillRect(x, y, 2, 2, occupied ? COLOR_RED : COLOR_WHITE);
}

static void drawSingleDigit(uint8_t x, uint8_t y, uint8_t value, Rgb color) {
  drawChar(x, y, '0' + min((uint8_t)9, value), color);
}

static void drawTotal(uint8_t x, uint8_t y, uint8_t value, Rgb color) {
  value = min((uint8_t)99, value);
  if (value < 10) {
    drawSingleDigit(x + 2, y, value, color);
    return;
  }

  drawSingleDigit(x, y, value / 10, color);
  drawSingleDigit(x + 4, y, value % 10, color);
}

static void drawScoreRow(
  uint8_t y,
  const uint8_t inningRuns[3],
  uint8_t total,
  Rgb color
) {
  fillRect(66, y, 5, 6, COLOR_BLACK);
  fillRect(72, y, 5, 6, COLOR_BLACK);
  fillRect(78, y, 5, 6, COLOR_BLACK);
  fillRect(86, y, 8, 6, COLOR_BLACK);

  drawSingleDigit(67, y, inningRuns[0], color);
  drawSingleDigit(73, y, inningRuns[1], color);
  drawSingleDigit(79, y, inningRuns[2], color);
  drawTotal(87, y, total, color);
}

static void renderFramebuffer(const ScoreboardState &state) {
  drawTemplate();

  drawSingleDigit(0, 0, state.balls, COLOR_CYAN);
  drawSingleDigit(0, 5, state.strikes, COLOR_YELLOW);
  drawSingleDigit(0, 10, state.outs, COLOR_CYAN);

  drawBaseBox(37, 1, state.runnerSecond);
  drawBaseBox(43, 7, state.runnerFirst);
  drawBaseBox(31, 7, state.runnerThird);

  drawScoreRow(1, state.awayInningRuns, state.awayScore, COLOR_CYAN);
  drawScoreRow(9, state.homeInningRuns, state.homeScore, COLOR_YELLOW);
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

static size_t buildZlibCompressedBlock(
  uint8_t *out,
  size_t capacity,
  const uint8_t *data,
  size_t length
) {
  const int flags =
    TDEFL_WRITE_ZLIB_HEADER |
    TDEFL_DEFAULT_MAX_PROBES;
  return tdefl_compress_mem_to_mem(out, capacity, data, length, flags);
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

  size_t minizPngLength = 0;
  void *minizPng = tdefl_write_image_to_png_file_in_memory_ex(
    pixelBuffer,
    SCOREBOARD_WIDTH,
    SCOREBOARD_HEIGHT,
    3,
    &minizPngLength,
    6,
    false
  );
  if (minizPng != nullptr && minizPngLength > 0 && minizPngLength <= outCapacity) {
    memcpy(out, minizPng, minizPngLength);
    mz_free(minizPng);
    Serial.print("Scoreboard PNG: png=");
    Serial.print(minizPngLength);
    Serial.println(" mode=miniz-png");
    return minizPngLength;
  }
  if (minizPng != nullptr) {
    mz_free(minizPng);
  }
  Serial.println("Scoreboard PNG: miniz-png failed; using fallback encoder");

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

  static uint8_t idatBuffer[SCOREBOARD_PNG_MAX_BYTES];
  size_t idatLength =
    buildZlibCompressedBlock(idatBuffer, sizeof(idatBuffer), pngRawBuffer, rawPos);
  bool compressed = idatLength != 0;
  if (idatLength == 0) {
    idatLength =
      buildZlibStoredBlock(idatBuffer, sizeof(idatBuffer), pngRawBuffer, rawPos);
  }
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

  Serial.print("Scoreboard PNG: raw=");
  Serial.print(rawPos);
  Serial.print(" idat=");
  Serial.print(idatLength);
  Serial.print(" png=");
  Serial.print(pos);
  Serial.print(" mode=");
  Serial.println(compressed ? "compressed" : "stored");

  return pos;
}
