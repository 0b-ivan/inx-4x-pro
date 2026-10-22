#pragma once
#include <memory>
#include <Memory.h>
#include <string>
#include <vector>

#include "../../activities/Activity.h"
#include "../../util/ButtonNavigator.h"

class UnitConverterActivity final : public Activity {
 public:
  explicit UnitConverterActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("UnitConverter", renderer, mappedInput) {}

  static std::unique_ptr<Activity> create(GfxRenderer& r, MappedInputManager& m) {
    return makeUniqueNoThrow<UnitConverterActivity>(r, m);
  }

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum State { SELECT_CATEGORY, SELECT_UNIT, INPUT_VALUE, RESULTS };

  State state = SELECT_CATEGORY;
  int categoryIndex = 0;
  int unitIndex = 0;
  ButtonNavigator buttonNavigator;

  std::string valueStr;
  int cursorPos = 0;

  struct UnitDef {
    const char* name;
    const char* abbrev;
    double toBase;
    double offset;
  };

  struct Category {
    const char* name;
    std::vector<UnitDef> units;
  };

  static const std::vector<Category>& getCategories();
  std::vector<std::string> conversionResults;

  void computeConversions();

  void renderCategorySelect() const;
  void renderUnitSelect() const;
  void renderInputValue() const;
  void renderResults() const;
};
