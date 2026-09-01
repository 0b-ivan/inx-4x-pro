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
      {"Tarot", "Tarot"},
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
      {"Bottom Tabs", "Untere Tabs"}, {"Flow", "Karussell"},
      {"12 hour", "12 Stunden"}, {"24 hour", "24 Stunden"},
      {"Front Button", "Vordere Tasten"}, {"Main Menu Buttons", "Hauptmenü-Tasten"},
      {"Front (Left/Right)", "Vorne (Links/Rechts)"}, {"Side (Up/Down)", "Seite (Hoch/Runter)"},
      {"Flick page turn", "Umblättern durch Bewegung"}, {"Flick sensitivity", "Bewegungsempfindlichkeit"},
      {"Normal", "Normal"}, {"Off", "Aus"},
      {"Refresh on load (Recent)", "Beim Öffnen aktualisieren (Zuletzt)"},
      {"Refresh on load (Library)", "Beim Öffnen aktualisieren (Bibliothek)"},
      {"Refresh on load (Settings)", "Beim Öffnen aktualisieren (Einstellungen)"},
      {"Refresh on load (Sync)", "Beim Öffnen aktualisieren (Sync)"},
      {"Refresh on load (Stats)", "Beim Öffnen aktualisieren (Statistik)"},
      {"Tarot images", "Tarotbilder"}, {"Download 78 tarot cards?", "78 Tarotkarten herunterladen?"},
      {"About 14 MB will be stored on the SD card.", "Etwa 14 MB werden auf der SD-Karte gespeichert."},
      {"Existing verified files are kept.", "Bereits geprüfte Dateien bleiben erhalten."},
      {"Downloading...", "Wird heruntergeladen ..."}, {"Tarot images installed", "Tarotbilder installiert"},
      {"All files passed SHA-256 verification.", "Alle Dateien haben die SHA-256-Prüfung bestanden."},
      {"Download failed", "Download fehlgeschlagen"},
      {"Retry resumes verified files.", "Ein neuer Versuch übernimmt bereits geprüfte Dateien."},
      {"Manifest could not be downloaded", "Manifest konnte nicht heruntergeladen werden"},
      {"Invalid manifest", "Ungültiges Manifest"}, {"Unsafe manifest entry", "Unsicherer Manifest-Eintrag"},
      {"Download or SHA-256 check failed", "Download oder SHA-256-Prüfung fehlgeschlagen"},
      {"Could not install downloaded file", "Heruntergeladene Datei konnte nicht installiert werden"},
      {"78 cards", "78 Karten"}, {"Tap to draw a card", "Tippen, um eine Karte zu ziehen"}, {"Draw", "Ziehen"},
      {"History", "Verlauf"}, {"Meaning", "Bedeutung"}, {"Tarot history", "Tarot-Verlauf"},
      {"Tarot files missing", "Tarotdateien fehlen"},
      {"Press Select or tap to download about 14 MB", "Auswählen oder tippen, um etwa 14 MB herunterzuladen"},
      {"Download", "Herunterladen"}, {"No sleep images", "Keine Standby-Bilder"},
      {"No preview", "Keine Vorschau"}, {"Random: On", "Zufall: An"}, {"Random: Off", "Zufall: Aus"},
      {"Random", "Zufall"}, {"files", "Dateien"}, {"Unknown", "Unbekannt"},
      {"No meaning found.", "Keine Bedeutung gefunden."},
  };
  for (const auto& entry : entries) if (std::strcmp(text, entry.en) == 0) return entry.de;
  return text;
}
