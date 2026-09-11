#include "PlayerProfileScreen.h"

#include <cstdio>

#include "PlayerAvatar.h"

namespace playerprofileui {
namespace {

const char* className(const player::PlayerClass value) {
  switch (value) {
    case player::PlayerClass::Commander:
      return "COMMANDER";
    case player::PlayerClass::Strategist:
      return "STRATEGIST";
    case player::PlayerClass::Tactician:
      return "TACTICIAN";
    case player::PlayerClass::FortuneSeeker:
      return "FORTUNE SEEKER";
    case player::PlayerClass::AllRounder:
      return "ALL-ROUNDER";
  }
  return "ALL-ROUNDER";
}

int iabs(const int value) { return value < 0 ? -value : value; }

void drawLine(const GfxRenderer& renderer, int x0, int y0, const int x1, const int y1, const bool ink = true) {
  const int dx = iabs(x1 - x0);
  const int sx = x0 < x1 ? 1 : -1;
  const int dy = -iabs(y1 - y0);
  const int sy = y0 < y1 ? 1 : -1;
  int error = dx + dy;

  while (true) {
    renderer.drawPixel(x0, y0, ink);
    if (x0 == x1 && y0 == y1) break;
    const int doubled = 2 * error;
    if (doubled >= dy) {
      error += dy;
      x0 += sx;
    }
    if (doubled <= dx) {
      error += dx;
      y0 += sy;
    }
  }
}

void radarPoint(const int cx, const int cy, const int radius, const int axis, const int percent, int& x, int& y) {
  static constexpr int kDx[6] = {0, 87, 87, 0, -87, -87};
  static constexpr int kDy[6] = {-100, -50, 50, 100, 50, -50};
  x = cx + (kDx[axis] * radius * percent) / 10000;
  y = cy + (kDy[axis] * radius * percent) / 10000;
}

void drawRadar(const GfxRenderer& renderer, toybox::Screen& screen, const fui::Rect& box,
               const player::StyleProfile& style) {
  const int cx = box.x + box.width / 2;
  const int cy = box.y + box.height / 2 + 5;
  const int radius = 88;

  static constexpr int kGridLevels[4] = {25, 50, 75, 100};
  for (const int level : kGridLevels) {
    int firstX = 0;
    int firstY = 0;
    int previousX = 0;
    int previousY = 0;
    for (int axis = 0; axis < 6; ++axis) {
      int x = 0;
      int y = 0;
      radarPoint(cx, cy, radius, axis, level, x, y);
      if (axis == 0) {
        firstX = x;
        firstY = y;
      } else {
        drawLine(renderer, previousX, previousY, x, y);
      }
      previousX = x;
      previousY = y;
    }
    drawLine(renderer, previousX, previousY, firstX, firstY);
  }

  for (int axis = 0; axis < 6; ++axis) {
    int x = 0;
    int y = 0;
    radarPoint(cx, cy, radius, axis, 100, x, y);
    drawLine(renderer, cx, cy, x, y);
  }

  const uint8_t values[6] = {style.strategy, style.tactics, style.precision,
                             style.risk, style.endurance, style.versatility};
  int pointsX[6]{};
  int pointsY[6]{};
  for (int axis = 0; axis < 6; ++axis) {
    radarPoint(cx, cy, radius, axis, values[axis], pointsX[axis], pointsY[axis]);
  }
  for (int axis = 0; axis < 6; ++axis) {
    const int next = (axis + 1) % 6;
    drawLine(renderer, pointsX[axis], pointsY[axis], pointsX[next], pointsY[next]);
    drawLine(renderer, pointsX[axis] + 1, pointsY[axis], pointsX[next] + 1, pointsY[next]);
    renderer.fillRect(pointsX[axis] - 2, pointsY[axis] - 2, 5, 5, true);
  }

  fui::TextStyle label;
  label.font = toybox::kSmallFont;
  label.align = fui::TextAlign::Center;
  label.color = fui::Color::Black;

  screen.target().text(fui::makeRect(static_cast<int16_t>(cx - 58), static_cast<int16_t>(cy - radius - 28), 116, 22),
                       "STRATEGY", label);
  screen.target().text(fui::makeRect(static_cast<int16_t>(cx + 72), static_cast<int16_t>(cy - 62), 104, 22),
                       "TACTICS", label);
  screen.target().text(fui::makeRect(static_cast<int16_t>(cx + 72), static_cast<int16_t>(cy + 43), 104, 22),
                       "PRECISION", label);
  screen.target().text(fui::makeRect(static_cast<int16_t>(cx - 50), static_cast<int16_t>(cy + radius + 5), 100, 22),
                       "RISK", label);
  screen.target().text(fui::makeRect(static_cast<int16_t>(cx - 176), static_cast<int16_t>(cy + 43), 110, 22),
                       "ENDURANCE", label);
  screen.target().text(fui::makeRect(static_cast<int16_t>(cx - 176), static_cast<int16_t>(cy - 62), 110, 22),
                       "VERSATILITY", label);
}

}  // namespace

void buildPlayerProfile(toybox::Screen& screen, const Model& model, const GfxRenderer& renderer) {
  fui::HeaderProps header;
  header.title = "PLAYER PROFILE";
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  toybox::headerRule(screen);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  const fui::Rect identity = screen.takeTop(96, toybox::kGutter);
  screen.target().stroke(identity, fui::Paint::solid(fui::Color::Black), toybox::kHairline, 10);

  constexpr int16_t kFace = 56;
  const fui::Rect face = fui::makeRect(static_cast<int16_t>(identity.x + toybox::kGutter),
                                       static_cast<int16_t>(identity.y + (identity.height - kFace) / 2), kFace, kFace);
  if (model.callsign != nullptr && model.callsign[0] != '\0') {
    player::drawAvatar(screen.target(), face, model.callsign, player::AvatarSize::Row);
  }

  fui::TextStyle title;
  title.font = toybox::kUiFont;
  title.align = fui::TextAlign::Left;
  title.color = fui::Color::Black;

  fui::TextStyle small;
  small.font = toybox::kSmallFont;
  small.align = fui::TextAlign::Left;
  small.color = fui::Color::Black;

  const int16_t textX = static_cast<int16_t>(face.right() + toybox::kGutter);
  const int16_t textW = static_cast<int16_t>(identity.right() - toybox::kGutter - textX);
  screen.target().text(fui::makeRect(textX, identity.y + 7, textW, 34), model.name, title);
  screen.target().text(fui::makeRect(textX, identity.y + 40, textW, 23), model.callsign, small);
  screen.target().text(fui::makeRect(textX, identity.y + 66, textW, 20), model.guest ? "GUEST PROFILE" : "SAVED PROFILE",
                       small);

  const fui::Rect progression = screen.takeTop(82, toybox::kGutter);
  screen.target().stroke(progression, fui::Paint::solid(fui::Color::Black), toybox::kHairline, 8);

  char levelLine[64]{};
  std::snprintf(levelLine, sizeof(levelLine), "LEVEL %u   %lu XP", static_cast<unsigned>(model.progression.level),
                static_cast<unsigned long>(model.progression.xp));
  screen.target().text(fui::makeRect(progression.x + 10, progression.y + 8, progression.width - 20, 28), levelLine, title);
  screen.target().text(fui::makeRect(progression.x + 10, progression.y + 38, progression.width - 20, 22),
                       className(model.progression.playerClass), small);

  const uint32_t levelFloor = player::ProgressionSystem::xpForLevel(model.progression.level);
  const uint32_t levelCeiling = model.progression.level < player::ProgressionSystem::kMaxLevel
                                    ? player::ProgressionSystem::xpForLevel(model.progression.level + 1U)
                                    : levelFloor;
  const fui::Rect xpBar = fui::makeRect(static_cast<int16_t>(progression.x + 10),
                                        static_cast<int16_t>(progression.y + progression.height - 14),
                                        static_cast<int16_t>(progression.width - 20), 8);
  screen.target().stroke(xpBar, fui::Paint::solid(fui::Color::Black), 1, 0);
  if (levelCeiling > levelFloor && model.progression.xp >= levelFloor) {
    const uint32_t numerator = model.progression.xp - levelFloor;
    const uint32_t denominator = levelCeiling - levelFloor;
    const int fill = static_cast<int>((static_cast<uint64_t>(xpBar.width - 2) * numerator) / denominator);
    if (fill > 0) renderer.fillRect(xpBar.x + 1, xpBar.y + 1, fill, xpBar.height - 2, true);
  } else if (model.progression.level >= player::ProgressionSystem::kMaxLevel) {
    renderer.fillRect(xpBar.x + 1, xpBar.y + 1, xpBar.width - 2, xpBar.height - 2, true);
  }

  const fui::Rect battleship = screen.takeTop(102, toybox::kGutter);
  screen.target().stroke(battleship, fui::Paint::solid(fui::Color::Black), toybox::kHairline, 8);
  screen.target().text(fui::makeRect(battleship.x + 10, battleship.y + 7, battleship.width - 20, 26), "BATTLESHIP", title);

  const char* rank = model.battleshipRank;
  if (rank == nullptr || rank[0] == '\0') rank = "NO RANK YET";
  screen.target().text(fui::makeRect(battleship.x + 10, battleship.y + 34, battleship.width - 20, 22), rank, small);

  char record[96]{};
  std::snprintf(record, sizeof(record), "%lu W   %lu L   %lu D   BEST STREAK %lu",
                static_cast<unsigned long>(model.battleship.wins), static_cast<unsigned long>(model.battleship.losses),
                static_cast<unsigned long>(model.battleship.draws),
                static_cast<unsigned long>(model.battleship.bestStreak));
  screen.target().text(fui::makeRect(battleship.x + 10, battleship.y + 59, battleship.width - 20, 22), record, small);

  char nextRank[64]{};
  if (model.nextBattleshipRankWins > 0) {
    std::snprintf(nextRank, sizeof(nextRank), "NEXT RANK AT %lu WINS",
                  static_cast<unsigned long>(model.nextBattleshipRankWins));
  } else {
    std::snprintf(nextRank, sizeof(nextRank), "MAX BATTLESHIP RANK");
  }
  screen.target().text(fui::makeRect(battleship.x + 10, battleship.y + 80, battleship.width - 20, 18), nextRank, small);

  const fui::Rect radar = screen.takeTop(300, 0);
  fui::TextStyle radarTitle = title;
  radarTitle.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(radar.x, radar.y, radar.width, 28), "PLAY STYLE", radarTitle);
  const fui::Rect radarPlot = fui::makeRect(radar.x, static_cast<int16_t>(radar.y + 31), radar.width,
                                            static_cast<int16_t>(radar.height - 31));
  drawRadar(renderer, screen, radarPlot, model.progression.style);
}

}  // namespace playerprofileui
