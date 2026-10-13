#include "hold.h"

#include <Arduino.h>

#include "ui.h"

namespace hold {

namespace {
constexpr uint32_t kDrawEveryMs = 60;  // чаще перерисовывать смысла нет
}  // namespace

Event Watcher::poll() {
  const uint32_t now = millis();
  const holdbtn::Hold::Event e = logic_.poll(now, button_.isPressed(),
                                       button_.wasPressed(),
                                       button_.wasReleased());

  switch (e) {
    case holdbtn::Hold::Event::Progress:
      if (showBar_ && now - lastDraw_ >= kDrawEveryMs) {
        lastDraw_ = now;
        ui::holdProgress(logic_.percent());
      }
      return Event::Progress;

    case holdbtn::Hold::Event::Fired:
      if (showBar_) ui::holdProgress(0);  // полоска своё отыграла
      return Event::Fired;

    case holdbtn::Hold::Event::Tap:
      if (showBar_) ui::holdProgress(0);
      return Event::Tap;

    case holdbtn::Hold::Event::None:
    default:
      return Event::None;
  }
}

void Watcher::clear() {
  if (showBar_) ui::holdProgress(0);
}

}  // namespace hold
