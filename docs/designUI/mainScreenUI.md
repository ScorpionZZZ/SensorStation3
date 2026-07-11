# ESP32 TFT Display — UI Specification
**Screen:** 240 × 320 px · Portrait · 16-bit color (RGB565)  
**Library target:** adaptable to LVGL  
**Style:** Dark Card theme (Dark Carbon × Light Minimal)

---

## 1. Color Palette

| Token | Hex | RGB | RGB565 | Usage |
|---|---|---|---|---|
| `COL_BG` | `#0B0C18` | 11, 12, 24 | `0x0863` | Screen background |
| `COL_CARD` | `#0E0F22` | 14, 15, 34 | `0x0864` | Card / header fill |
| `COL_BORDER` | `#1A1B35` | 26, 27, 53 | `0x18C6` | Card border, dividers |
| `COL_BORDER_DEEP` | `#252550` | 37, 37, 80 | `0x212A` | Settings button border |
| `COL_TEMP` | `#F97316` | 249, 115, 22 | `0xFB82` | Temperature accent |
| `COL_TEMP_DIM` | `#F9731633` | — | blend @ 20% | Sparkline fill area |
| `COL_HUM` | `#38BDF8` | 56, 189, 248 | `0x3DFF` | Humidity accent |
| `COL_HUM_DIM` | `#38BDF814` | — | blend @ 8% | Sparkline fill area |
| `COL_WIFI_OFF` | `#1E2042` | 30, 32, 66 | `0x1024` | Inactive WiFi bar |
| `COL_TIME` | `#D0D0F0` | 208, 208, 240 | `0xD69E` | Clock digits |
| `COL_LABEL` | `#383870` | 56, 56, 112 | `0x3816` | Section labels |
| `COL_DEW` | `#8080B0` | 128, 128, 176 | `0x8456` | Dew point value |
| `COL_PRESSURE` | `#6868A8` | 104, 104, 168 | `0x6455` | Pressure value |
| `COL_HUM_BADGE` | `#6090B8` | 96, 144, 184 | `0x6496` | Humidity trend badge |
| `COL_TREND_UP` | `#EF4444` | 239, 68, 68 | `0xEA28` | Rising trend arrow |
| `COL_TREND_BG` | `#1E0500` | 30, 5, 0 | `0x1800` | Rising trend badge bg |
| `COL_HUM_BADGE_BG` | `#001828` | 0, 24, 40 | `0x00C3` | Stable trend badge bg |
| `COL_SETTINGS` | `#4A4A80` | 74, 74, 128 | `0x4A50` | Settings icon + label |
| `COL_DATE` | `#333368` | 51, 51, 104 | `0x3194` | Date string |
| `COL_YEAR` | `#252558` | 37, 37, 88 | `0x212B` | Year string |

---

## 2. Typography

Map design sizes as follows:

| Design role | Approx px | Notes |
|---|---|---|
| Clock digits | 20 px | tabular nums |
| Temperature value | 44 px | bold, tight tracking |
| Humidity value | 38 px | bold |
| Dew point value | 15 px | medium weight |
| Pressure value | 14 px | bold |
| Section label | 7 px | UPPERCASE |
| Badge text | 7 px | UPPERCASE, tight |
| Settings label | 9 px | UPPERCASE |
| Date string | 7 px | UPPERCASE |

---

## 3. Layout — Zone Map

```
┌─────────────────────────────────────────┐  y=0
│  HEADER BAR                         h=34│
│  [date]                  [time][wifi]   │
├─────────────────────────────────────────┤  y=34
│  padding-top: 7 px                      │
│ ┌───────────────────────────────────┐   │  y=41
│ │ TEMPERATURE CARD             h=98 │   │
│ │ ← 3px accent border #F97316       │   │
│ └───────────────────────────────────┘   │  y=139
│  gap: 5 px                              │
│ ┌───────────────────────────────────┐   │  y=144
│ │ HUMIDITY CARD                h=76 │   │
│ │ ← 3px accent border #38BDF8       │   │
│ └───────────────────────────────────┘   │  y=220
│  gap: 5 px                              │
│ ┌───────────────────────────────────┐   │  y=225
│ │ DEW POINT CARD               h=40 │   │
│ └───────────────────────────────────┘   │  y=265
│  gap: 5 px                              │
│ ┌──────────────┐ ┌────────────────┐     │  y=270
│ │ PRESSURE h=43│ │ SETTINGS   h=43│     │
│ └──────────────┘ └────────────────┘     │  y=313
│  padding-bottom: 7 px                   │
└─────────────────────────────────────────┘  y=320
```

**Card geometry (all cards):**
- `x = 8`, `width = 224` (8 px margin each side)
- `border-radius = 5 px`
- `border = 1 px` → `COL_BORDER`
- Inner padding: `left = 10 px`, `right = 10 px`, `top = 7 px`, `bottom = 7 px`
- Temperature & humidity cards: **left accent border 3 px** wide → `COL_TEMP` / `COL_HUM`

---

## 4. Header Bar

```
x=0, y=0, w=240, h=34
fill: COL_CARD
bottom border: y=33, h=1, COL_BORDER
```

### 4.1 Date (left side)
| Element | x | y | Color | Font |
|---|---|---|---|---|
| `"MON, 23 JUN"` | 12 | 9 | `COL_DATE` | Font1, UPPERCASE, tracking |
| `"2025"` | 12 | 20 | `COL_YEAR` | Font1 |

### 4.2 Time + WiFi group (right side, right-aligned)

The time string and WiFi bars are right-aligned as a group with `gap = 7 px` between them.

**Time string `"--:--"`**
- Right edge of time text: `x ≈ 198`, `y_center = 17`
- Color: `COL_TIME`, bold, `font-variant-numeric: tabular-nums`
- Rendered width ≈ 52 px → text starts at `x ≈ 146`
- Semicolon blink

**WiFi bars** (5 bars)
- Anchor: `x_left = 207`, bars bottom at `y = 27`
- Bar width: `3 px`, gap between bars: `2 px`
- Heights (bottom-aligned): 5, 7, 10, 12, 14 px
- Active bars: `COL_HUM` (`#38BDF8`)
- Inactive bars: `COL_WIFI_OFF`

```
Bar index:  1    2    3    4    5
x offset:   0    5   10   15   20
height:     5    7   10   12   14
color:    ACTV ACTV ACTV  OFF  OFF
```

WiFi bar x positions: `207, 212, 217, 222, 227`

---

## 5. Temperature Card

```
x=8, y=41, w=224, h=98
fill: COL_CARD
border: 1px COL_BORDER, r=5
left accent: x=8, y=41+5=46, w=3, h=88, fill: COL_TEMP
inner content x-start: 21 (8 + 3 accent + 10 padding)
inner content x-end:   222 (232 - 10 padding)
```

### 5.1 Section label
`"TEMPERATURE"` at `x=21, y=55`, `COL_LABEL`, Font1, UPPERCASE, letter-spacing wide

### 5.2 Value row (baseline at `y ≈ 108`)
| Element | x | y | Color | Size |
|---|---|---|---|---|
| `"23.4"` | 21 | 68 | `COL_TEMP` | FreeSansBold18pt or equiv |
| `"°C"` | ~113 | 88 | `COL_TEMP` @ 40% alpha | smaller, bottom-aligned |
| Trend badge | right-aligned ~191 | 92 | — | see below |

**Trend badge** (rising example):
- `fillRoundRect(191, 88, 33, 16, 8, COL_TREND_BG)`
- `drawRoundRect(191, 88, 33, 16, 8, 0x3A0A00)` (border)
- Arrow `"↑"` at `x=196, y=99`, `COL_TREND_UP`, Font1
- Delta `"+1.2°"` at `x=203, y=99`, `COL_TREND_UP`, Font1, bold

**Trend states:**
| State | Arrow | Color | Badge BG |
|---|---|---|---|
| Rising | `↑` | `#EF4444` | `#1E0500` |
| Stable | `=` | `#6090B8` | `#001828` |
| Falling | `↓` | `#38BDF8` | `#001828` |

### 5.3 Sparkline (24 hours array)
```
x=21, y=114, w=201, h=16
```
- oldest → left, newest → right
- Bar width per sample: `201 / 10 = ~20 px`, gap `2 px` → effective bar `18 px`
- Normalize: `barH = map(value, histMin, histMax, 2, 16)`
- **Fill area** (closed polygon): `COL_TEMP` @ 12% alpha → approximate with stipple or blended color `0x8040` (orange-dark blend)
- **Line** on top: `COL_TEMP`, stroke 1.5 px (draw 2px thick line)
- Alternatively, render as 10 vertical bars bottom-aligned at `y_bottom=130`

---

## 6. Humidity Card

```
x=8, y=144, w=224, h=76
fill: COL_CARD
border: 1px COL_BORDER, r=5
left accent: x=8, y=149, w=3, h=66, fill: COL_HUM
inner content x-start: 21
```

### 6.1 Section label
`"HUMIDITY"` at `x=21, y=153`, `COL_LABEL`, Font1, UPPERCASE

### 6.2 Value row (baseline at `y ≈ 190`)
| Element | x | y | Color |
|---|---|---|---|
| `"65"` | 21 | 158 | `COL_HUM` |
| `"%"` | ~84 | 174 | `COL_HUM` @ 40% alpha |
| Trend badge | right-aligned | 174 | (see § 5.2 states) |

### 6.3 Sparkline
```
x=21, y=196, w=201, h=13
```
Same logic as temperature sparkline but:
- Color: `COL_HUM` / `COL_HUM_DIM`
- Stable trend → bars nearly equal height, slight noise ±2 px

---

## 7. Dew Point Card

```
x=8, y=225, w=224, h=40
fill: COL_CARD
border: 1px COL_BORDER, r=5
no left accent border
```

| Element | x | y | Color | Notes |
|---|---|---|---|---|
| `"DEW POINT"` label | 18 | 233 | `COL_LABEL` | Font1, UPPERCASE |
| `"16.2°C"` value | 18 | 248 | `COL_DEW` | FreeSans9pt or size 2 |
| Circle icon | `x=214, y=237` r=13 | — | fill `#111230`, border `COL_BORDER` | centered |
| `"💧"` or drop shape | center of circle | — | drawn in cyan/blue | 12 px |

> For the water drop icon without emoji support, draw a filled teardrop: `fillCircle(214,243,6,COL_HUM)` + `fillTriangle(208,242, 220,242, 214,232, COL_HUM)`

---

## 8. Bottom Row

Two equal-width cards side by side:
- Left card: `x=8, y=270, w=109, h=43`
- Gap: `5 px`
- Right card: `x=122, y=270, w=110, h=43`
- Both: `fill COL_CARD, border 1px COL_BORDER, r=5`

### 8.1 Pressure Card (left)
| Element | x | y | Color |
|---|---|---|---|
| `"PRESSURE"` label | 17 | 278 | `COL_LABEL` |
| `"1013"` value | 17 | 293 | `COL_PRESSURE` |
| `"hPa"` unit | ~55 | 293 | `COL_YEAR` (dim) |

### 8.2 Settings Button (right)
```
x=122, y=270, w=110, h=43
fill: COL_CARD
border: 1px COL_BORDER_DEEP (#252550)
r=5
Content centered: icon + label side by side
```

| Element | offset from center | Color |
|---|---|---|
| Gear icon (⚙ or drawn) | x=155, y=291 | `COL_SETTINGS` |
| `"SETTINGS"` text | x=168, y=291 | `COL_SETTINGS` |


---

## 9. Dynamic Data Specification



### 9.1 WiFi Signal → Bars Mapping

Use this algorithm

-  if (rssi >= -50) return 5;
-  else if (rssi >= -60) return 4;
-  else if (rssi >= -70) return 3;
-  else if (rssi >= -80) return 2;
-  else 1;

Draw active bars with `COL_HUM`, inactive with `COL_WIFI_OFF`. Always draw all 5 bars.

---


## 10. Quick Reference — Key Coordinates

| Element | x | y | w | h |
|---|---|---|---|---|
| Header bar | 0 | 0 | 240 | 34 |
| Temperature card | 8 | 41 | 224 | 98 |
| Temp left accent | 8 | 46 | 3 | 88 |
| Temp label | 21 | 55 | — | — |
| Temp value | 21 | 68 | — | — |
| Temp sparkline | 21 | 114 | 201 | 16 |
| Humidity card | 8 | 144 | 224 | 76 |
| Hum left accent | 8 | 149 | 3 | 66 |
| Hum label | 21 | 153 | — | — |
| Hum value | 21 | 158 | — | — |
| Hum sparkline | 21 | 196 | 201 | 13 |
| Dew point card | 8 | 225 | 224 | 40 |
| Dew label | 18 | 233 | — | — |
| Dew value | 18 | 248 | — | — |
| Pressure card | 8 | 270 | 109 | 43 |
| Settings card | 122 | 270 | 110 | 43 |
| Clock text | 146 | 7 | ~52 | 20 |
| WiFi bar 1 | 207 | 27 | 3 | 5 |
| WiFi bar 2 | 212 | 25 | 3 | 7 |
| WiFi bar 3 | 217 | 21 | 3 | 10 |
| WiFi bar 4 | 222 | 19 | 3 | 12 |
| WiFi bar 5 | 227 | 17 | 3 | 14 |