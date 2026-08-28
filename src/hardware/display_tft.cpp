#include "display_tft.h"

#include <TFT_eSPI.h>

#include <cstdio>
#include <cstring>

#include "format.h"

namespace dpf
{

  namespace
  {
    TFT_eSPI tft;

    // ---- boot/text screen (showLines) ----
    // The boot text is pre-aligned in 20-char columns, so it uses the monospaced
    // GLCD font (font 1) at size 2, matching the original layout.
    constexpr uint8_t kTextFont = 1;
    constexpr uint8_t kTextSize = 2;
    constexpr int kLineHeight = 52;
    constexpr int kMarginX = 6;
    constexpr int kMarginY = 12;

    // ---- colours (RGB565) ----
    constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
    {
      return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }
    constexpr uint16_t kBg = rgb(0, 0, 0);
    constexpr uint16_t kFanOn = rgb(28, 150, 58);
    constexpr uint16_t kFanOff = rgb(70, 74, 82);
    constexpr uint16_t kAccent = rgb(46, 158, 240);
    constexpr uint16_t kInside = rgb(90, 200, 255);
    constexpr uint16_t kOutside = rgb(255, 165, 60);
    constexpr uint16_t kValue = rgb(240, 240, 240);
    constexpr uint16_t kDim = rgb(110, 110, 110);
    constexpr uint16_t kReason = rgb(240, 210, 90);
    constexpr uint16_t kLabel = rgb(150, 155, 165);
    constexpr uint16_t kNavy = rgb(22, 44, 84);
    constexpr uint16_t kBtnOff = rgb(35, 38, 45);
    constexpr uint16_t kBorder = rgb(70, 74, 82);

    // ---- fonts ----
    constexpr uint8_t kBig = 4; // ~26 px, big numbers
    constexpr uint8_t kMed = 2; // ~16 px, labels / small values

    // ---- dynamic-field cache: only repaint a field when its text changes ----
    enum FieldId
    {
      kFFan,
      kFOverride,
      kFTempIn,
      kFTempOut,
      kFHumIn,
      kFHumOut,
      kFDpIn,
      kFDpOut,
      kFDiff,
      kFReason,
      kFRssiIn,
      kFRssiOut,
      kFBatIn,
      kFBatOut,
      kFUpIn,
      kFUpOut,
      kFSeenIn,
      kFSeenOut,
      kFIp,
      kFieldCount
    };
    constexpr int kFieldCacheLen = 48;  // must exceed the longest field (the IP/SSID line)
    char g_cache[kFieldCount][kFieldCacheLen];

    void clearCache()
    {
      for (auto &c : g_cache)
      {
        c[0] = '\x01'; // sentinel != any real string
        c[1] = '\0';
      }
    }

    // Draw a value into a fixed slot, erasing the old glyphs via text padding.
    // Skips the draw when the text is unchanged since last time.
    void field(FieldId id, const char *s, int x, int y, uint8_t font,
               uint16_t fg, uint16_t bg, uint8_t datum, uint16_t pad)
    {
      if (strncmp(g_cache[id], s, kFieldCacheLen) == 0)
        return;
      snprintf(g_cache[id], kFieldCacheLen, "%s", s);
      tft.setTextColor(fg, bg);
      tft.setTextDatum(datum);
      tft.setTextPadding(pad);
      tft.drawString(s, x, y, font);
    }

    void fmtI(char *buf, size_t cap, long v, const char *suffix, bool valid)
    {
      if (!valid)
      {
        std::snprintf(buf, cap, "--");
        return;
      }
      std::snprintf(buf, cap, "%ld%s", v, suffix);
    }

    bool fresh(const SensorData &s, double now)
    {
      return s.hasReading && (now - s.scanned) <= kFreshnessSeconds;
    }

    int lastSeen(const SensorData &s, double now)
    {
      if (!s.hasReading)
        return 9999;
      double secs = now - s.scanned;
      if (secs < 0)
        secs = 0;
      return secs > 9999 ? 9999 : static_cast<int>(secs);
    }

    const char *overrideBadge(int mode)
    {
      switch (mode)
      {
      case 1:
        return "MANUAL ON";
      case 2:
        return "MANUAL OFF";
      default:
        return "AUTO";
      }
    }

    // Dashboard column geometry.
    constexpr int kColInX = 80;
    constexpr int kColOutX = 240;
    constexpr int kColPad = 150;

    // Draw the big temperature reading centred on (cx, y): the value in Font4,
    // then a hand-drawn degree ring + "C" (Font4 has no `°` glyph, unlike the
    // Font2 dew-point line). `num` is the unit-less value ("26.0" or "--").
    // Owns its cache slot like field(); skips the redraw when nothing changed.
    void tempField(FieldId id, const char *num, bool valid, int cx,
                   int y, uint16_t fg, uint16_t bg)
    {
      // Build cache key: append a sentinel byte when fg != kValue to
      // force redraw on colour change even when the number is the same.
      char key[32];
      std::snprintf(key, sizeof(key), "%s%s", num,
                    (fg == kValue ? "" : "\x02"));
      if (strncmp(g_cache[id], key, kFieldCacheLen) == 0)
        return;
      snprintf(g_cache[id], kFieldCacheLen, "%s", key);

      // Erase the whole slot; we position glyphs manually below.
      const int h = 28; // Font4 is ~26 px tall
      tft.fillRect(cx - kColPad / 2, y - h / 2, kColPad, h, bg);
      tft.setTextColor(fg, bg);
      tft.setTextPadding(0);

      if (!valid)
      {
        tft.setTextDatum(MC_DATUM);
        tft.drawString("--", cx, y, kBig);
        return;
      }

      // Lay out "<num>" + gap + ring + "C", horizontally centred as a group.
      constexpr int kRingR = 3;
      constexpr int kGap = 3;
      const int wNum = tft.textWidth(num, kBig);
      const int wC = tft.textWidth("C", kBig);
      const int wRing = 2 * kRingR + 1;
      const int total = wNum + kGap + wRing + kGap + wC;
      int x = cx - total / 2;

      tft.setTextDatum(ML_DATUM);
      tft.drawString(num, x, y, kBig);
      x += wNum + kGap;

      const int ringCx = x + kRingR;
      const int ringCy = y - h / 2 + kRingR + 2; // near the glyph top
      tft.drawCircle(ringCx, ringCy, kRingR, fg);
      tft.drawCircle(ringCx, ringCy, kRingR - 1, fg);
      x += wRing + kGap;

      tft.drawString("C", x, y, kBig);
    }
  } // namespace

  void DisplayTft::begin()
  {
    tft.init();
    tft.setRotation(1); // 320x240 landscape (use 3 if the panel is upside-down)
    tft.fillScreen(kBg);
    tft.setTextColor(kValue, kBg);
    tft.setTextFont(kTextFont);
    tft.setTextSize(kTextSize);
  }

  void DisplayTft::showLines(const ScreenLines &lines)
  {
    screen_ = Screen::Boot;
    tft.fillScreen(kBg);
    tft.setTextDatum(TL_DATUM);
    tft.setTextPadding(0);
    tft.setTextColor(kValue, kBg);
    tft.setTextFont(kTextFont);
    tft.setTextSize(kTextSize);
    int y = kMarginY;
    for (const auto &line : lines)
    {
      tft.setCursor(kMarginX, y);
      tft.print(line.c_str());
      y += kLineHeight;
    }
  }

  void DisplayTft::drawButtons(int overrideMode)
  {
    if (buttonMode_ == overrideMode)
      return;
    buttonMode_ = overrideMode;
    const char *labels[ui::kBtnCount] = {"AUTO", "ON", "OFF"};
    const int w = ui::kScreenW / ui::kBtnCount;
    for (int i = 0; i < ui::kBtnCount; ++i)
    {
      const int x = i * w;
      const bool active = (overrideMode == i);
      const uint16_t fill = active ? kAccent : kBtnOff;
      const uint16_t txt = active ? kBg : kLabel;
      tft.fillRoundRect(x + 2, ui::kBtnY, w - 4, ui::kBtnH, 4, fill);
      tft.drawRoundRect(x + 2, ui::kBtnY, w - 4, ui::kBtnH, 4, kBorder);
      tft.setTextColor(txt, fill);
      tft.setTextDatum(MC_DATUM);
      tft.setTextPadding(0);
      tft.drawString(labels[i], x + w / 2, ui::kBtnY + ui::kBtnH / 2, kMed);
    }
  }

  void DisplayTft::showDashboard(const SensorData &inside,
                                 const SensorData &outside,
                                 const ResultData &result,
                                 const FanConfig &fanConfig, int overrideMode,
                                 double now)
  {
    if (screen_ != Screen::Dashboard)
    {
      screen_ = Screen::Dashboard;
      clearCache();
      bannerColor_ = 0;
      buttonMode_ = -1;
      tft.fillScreen(kBg);
      tft.setTextSize(1);
      // static chrome: column headers + divider
      tft.setTextDatum(MC_DATUM);
      tft.setTextPadding(0);
      tft.setTextColor(kInside, kBg);
      tft.drawString("INSIDE", kColInX, 50, kMed);
      tft.setTextColor(kOutside, kBg);
      tft.drawString("OUTSIDE", kColOutX, 50, kMed);
      tft.drawFastVLine(160, 42, 100, kBorder);
    }

    // banner: colour tracks the actual relay state
    const uint16_t bc = result.isOn ? kFanOn : kFanOff;
    if (bc != bannerColor_)
    {
      bannerColor_ = bc;
      tft.fillRect(0, 0, ui::kScreenW, ui::kBannerH, bc);
      g_cache[kFFan][0] = '\x01'; g_cache[kFFan][1] = '\0';
      g_cache[kFOverride][0] = '\x01'; g_cache[kFOverride][1] = '\0';
    }
    const int bannerY = ui::kBannerH / 2 + 3;
    field(kFFan, result.isOn ? "FAN ON" : "FAN OFF", 10, bannerY, kBig,
          kValue, bc, ML_DATUM, 0);
    field(kFOverride, overrideBadge(overrideMode), ui::kScreenW - 10,
          bannerY, kMed, kValue, bc, MR_DATUM, 140);

    const bool inOk = fresh(inside, now);
    const bool outOk = fresh(outside, now);
    const uint16_t inCol = inOk ? kValue : kDim;
    const uint16_t outCol = outOk ? kValue : kDim;

    char buf[48];
    formatMeasure(buf, sizeof(buf), inside.temperature, "", inOk);
    tempField(kFTempIn, buf, inOk, kColInX, 80, inCol, kBg);
    formatMeasure(buf, sizeof(buf), outside.temperature, "", outOk);
    tempField(kFTempOut, buf, outOk, kColOutX, 80, outCol, kBg);
    formatMeasure(buf, sizeof(buf), inside.humidity, "%", inOk);
    field(kFHumIn, buf, kColInX, 110, kMed, inCol, kBg, MC_DATUM, kColPad);
    formatMeasure(buf, sizeof(buf), outside.humidity, "%", outOk);
    field(kFHumOut, buf, kColOutX, 110, kMed, outCol, kBg, MC_DATUM, kColPad);

    // "DP " + formatted value
    char dpBuf[48];
    formatMeasure(dpBuf, sizeof(dpBuf), inside.dewPoint, "`C", inOk);
    char dpInBuf[64];
    std::snprintf(dpInBuf, sizeof(dpInBuf), "DP %s", dpBuf);
    field(kFDpIn, dpInBuf, kColInX, 132, kMed, inCol, kBg, MC_DATUM, kColPad);

    formatMeasure(dpBuf, sizeof(dpBuf), outside.dewPoint, "`C", outOk);
    char dpOutBuf[64];
    std::snprintf(dpOutBuf, sizeof(dpOutBuf), "DP %s", dpBuf);
    field(kFDpOut, dpOutBuf, kColOutX, 132, kMed, outCol, kBg, MC_DATUM, kColPad);

    const bool diffOk = inOk && outOk;
    const double diff = inside.dewPoint - outside.dewPoint;
    const double thr = fanConfig.minDiff + fanConfig.hysteresis;
    formatDiff(buf, sizeof(buf), diff, thr, diffOk);
    field(kFDiff, buf, 160, 156, kMed, kValue, kBg, MC_DATUM, ui::kScreenW);
    field(kFReason, reasonName(result.reason), 160, 186, kBig, kReason, kBg,
          MC_DATUM, ui::kScreenW);

    drawButtons(overrideMode);
  }

  void DisplayTft::showDiagnostics(const SensorData &inside,
                                   const SensorData &outside,
                                   int overrideMode,
                                   const std::string &ip,
                                   const std::string &ssid,
                                   bool online,
                                   double now)
  {
    if (screen_ != Screen::Diagnostics)
    {
      screen_ = Screen::Diagnostics;
      clearCache();
      buttonMode_ = -1;
      tft.fillScreen(kBg);
      tft.setTextSize(1);
      tft.fillRect(0, 0, ui::kScreenW, ui::kBannerH, kNavy);
      tft.setTextDatum(MC_DATUM);
      tft.setTextPadding(0);
      tft.setTextColor(kValue, kNavy);
      tft.drawString("DIAGNOSTICS", ui::kScreenW / 2, ui::kBannerH / 2 + 3, kBig);
      // column headers + row labels (static)
      tft.setTextColor(kInside, kBg);
      tft.drawString("INSIDE", 175, 50, kMed);
      tft.setTextColor(kOutside, kBg);
      tft.drawString("OUTSIDE", 265, 50, kMed);
      tft.setTextDatum(ML_DATUM);
      tft.setTextColor(kLabel, kBg);
      tft.drawString("RSSI", 10, 76, kMed);
      tft.drawString("Battery", 10, 102, kMed);
      tft.drawString("Uptime", 10, 128, kMed);
      tft.drawString("Last seen", 10, 154, kMed);
    }

    const bool inHas = inside.hasReading;
    const bool outHas = outside.hasReading;
    const int rowPad = 84;

    char buf[32];
    fmtI(buf, sizeof(buf), inside.rssi, "", inHas);
    field(kFRssiIn, buf, 175, 76, kMed, kValue, kBg, MC_DATUM, rowPad);
    fmtI(buf, sizeof(buf), outside.rssi, "", outHas);
    field(kFRssiOut, buf, 265, 76, kMed, kValue, kBg, MC_DATUM, rowPad);
    fmtI(buf, sizeof(buf), inside.batLevel, "mV", inHas);
    field(kFBatIn, buf, 175, 102, kMed, kValue, kBg, MC_DATUM, rowPad);
    fmtI(buf, sizeof(buf), outside.batLevel, "mV", outHas);
    field(kFBatOut, buf, 265, 102, kMed, kValue, kBg, MC_DATUM, rowPad);
    fmtI(buf, sizeof(buf), inside.uptime / 86400, "d", inHas);
    field(kFUpIn, buf, 175, 128, kMed, kValue, kBg, MC_DATUM, rowPad);
    fmtI(buf, sizeof(buf), outside.uptime / 86400, "d", outHas);
    field(kFUpOut, buf, 265, 128, kMed, kValue, kBg, MC_DATUM, rowPad);
    fmtI(buf, sizeof(buf), lastSeen(inside, now), "s", inHas);
    field(kFSeenIn, buf, 175, 154, kMed, kValue, kBg, MC_DATUM, rowPad);
    fmtI(buf, sizeof(buf), lastSeen(outside, now), "s", outHas);
    field(kFSeenOut, buf, 265, 154, kMed, kValue, kBg, MC_DATUM, rowPad);

    char ipBuf[80];
    formatNetLine(ipBuf, sizeof(ipBuf), ip.c_str(), ssid.c_str(), online);
    field(kFIp, ipBuf, 10, 180, kMed, kLabel, kBg, ML_DATUM, ui::kScreenW);

    drawButtons(overrideMode);
  }

} // namespace dpf
