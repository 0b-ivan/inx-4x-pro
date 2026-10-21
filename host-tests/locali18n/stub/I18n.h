#pragma once
// Just enough of lib/I18n for LocalI18n.h to see a language. The real I18n
// pulls in generated tables and the SD card; the catalogue needs neither.
enum class Language { EN, DE };
struct HostI18n {
  Language language = Language::DE;
  Language getLanguage() const { return language; }
};
inline HostI18n I18N;
