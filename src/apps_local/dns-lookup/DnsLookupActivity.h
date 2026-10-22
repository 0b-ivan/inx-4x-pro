#pragma once
#include <memory>
#include <Memory.h>

#include <string>

#include "../../activities/Activity.h"

class DnsLookupActivity final : public Activity {
 public:
  explicit DnsLookupActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)

  static std::unique_ptr<Activity> create(GfxRenderer& r, MappedInputManager& m) {
    return makeUniqueNoThrow<DnsLookupActivity>(r, m);
  }
      : Activity("DnsLookup", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum State { TEXT_INPUT, RESOLVING, RESULTS };

  State state = TEXT_INPUT;
  std::string hostname;
  std::string resolvedIP;
  unsigned long resolutionTimeMs = 0;
  bool resolutionFailed = false;

  void doResolve();
  void renderResults() const;
};
