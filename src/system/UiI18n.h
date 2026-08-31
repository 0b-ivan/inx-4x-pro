#pragma once

#include <cstring>

#include "../state/SystemSetting.h"

// Lightweight English/German UI catalogue for the Inx application layer.
// CrossPoint uses generated string IDs; Inx still passes literal labels around,
// so this compatibility catalogue localizes those labels at UI boundaries.
inline const char* uiTr(const char* text) {
  if (!text || SETTINGS.uiLanguage != SystemSetting::UI_LANGUAGE_GERMAN) return text;
  struct Entry { const char* en; const char* de; };
  static constexpr Entry entries[] = {
      {"Settings", "Einstellungen"}, {"System settings", "Systemeinstellungen"},
      {"Reader", "Lesen"}, {"Display", "Anzeige"}, {"Clock", "Uhr"}, {"Image", "Bild"},
      {"Buttons", "Tasten"}, {"Device", "Gerät"}, {"Actions", "Aktionen"},
      {"Language", "Sprache"}, {"English", "Englisch"}, {"German", "Deutsch"},
      {"Sleep Screen", "Standby-Bild"}, {"Choose sleep image", "Standby-Bild wählen"},
      {"Hide Battery %", "Batterie % ausblenden"}, {"Theme", "Design"},
      {"Recent Library Mode", "Ansicht zuletzt gelesen"}, {"Library Mode", "Bibliotheksansicht"},
      {"Shelf mode", "Regalansicht"}, {"Hide button hints", "Tastenhinweise ausblenden"},
      {"Recent books shown", "Anzahl letzter Bücher"}, {"Show Clock", "Uhr anzeigen"},
      {"Face", "Zifferblatt"}, {"Format", "Format"}, {"Time zone", "Zeitzone"},
      {"Sync", "Synchronisieren"}, {"Cover Mode", "Cover-Modus"},
      {"Cover Filter", "Cover-Filter"}, {"Sleep Image Quality", "Standby-Bildqualität"},
      {"Thumbnail corners", "Vorschaubild-Ecken"}, {"Short Power Button Click", "Kurzer Druck auf Power"},
      {"Time to Sleep", "Standby nach"}, {"Use Index for Library", "Bibliotheksindex verwenden"},
      {"Library custom sort", "Eigene Bibliothekssortierung"}, {"Boot Mode", "Startansicht"},
      {"Delete Cache", "Cache löschen"}, {"Index your library", "Bibliothek indexieren"},
      {"Generate thumbnails", "Vorschaubilder erzeugen"}, {"About", "Über"},
      {"Dark", "Dunkel"}, {"Light", "Hell"}, {"Custom", "Benutzerdefiniert"},
      {"Recent Book", "Letztes Buch"}, {"Transparent Cover", "Transparentes Cover"},
      {"None", "Keine"}, {"Date Time", "Datum und Uhrzeit"}, {"Never", "Nie"},
      {"In Reader", "Beim Lesen"}, {"Always", "Immer"}, {"Classic", "Klassisch"},
      {"List", "Liste"}, {"Grid", "Raster"}, {"Icons", "Symbole"}, {"Cover", "Cover"},
      {"Fill", "Füllen"}, {"Crop", "Zuschneiden"}, {"Contrast", "Kontrast"},
      {"Inverted", "Invertiert"}, {"Low", "Niedrig"}, {"Medium", "Mittel"}, {"High", "Hoch"},
      {"Square", "Eckig"}, {"Rounded", "Rund"}, {"Subtle", "Dezent"},
      {"Ignore", "Ignorieren"}, {"Sleep", "Standby"}, {"Page Refresh", "Seite aktualisieren"},
      {"Recent Books", "Letzte Bücher"}, {"Home Page", "Startseite"},
      {"Choose dictionary", "Wörterbuch wählen"}, {"No dictionaries found.", "Keine Wörterbücher gefunden."},
      {"Select", "Auswählen"}, {"Toggle", "Umschalten"}, {"Close", "Schließen"},
      {"Exit", "Beenden"}, {"Save", "Speichern"}, {"Saved", "Gespeichert"},
      {"Look up", "Nachschlagen"}, {"Highlight", "Markieren"}, {"Dictionary", "Wörterbuch"},
      {"Translate", "Übersetzen"}, {"Cancel", "Abbrechen"}, {"Back", "Zurück"},
      {"Up", "Hoch"}, {"Down", "Runter"}, {"Prev", "Zurück"}, {"Next", "Weiter"},
      {"ON", "AN"}, {"OFF", "AUS"}, {"Sync time", "Uhrzeit synchronisieren"},
      {"Select network", "Netzwerk wählen"}, {"Time synced", "Uhrzeit synchronisiert"},
      {"Time sync failed", "Synchronisierung fehlgeschlagen"},
  };
  for (const auto& entry : entries) if (std::strcmp(text, entry.en) == 0) return entry.de;
  return text;
}
