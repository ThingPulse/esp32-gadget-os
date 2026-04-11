#include "ui/I18n.h"

#include "services/ConfigStoreService.h"

#include <cstring>

namespace I18n {

static Language g_language = Language::English;

static constexpr const char *kTranslations[(int)TextId::Count][(int)Language::Count] = {
  {"Settings", "Einstellungen", "Paramètres", "Ajustes"},
  {"Time", "Zeit", "Heure", "Hora"},
  {"Weather", "Wetter", "Météo", "Tiempo"},
  {"Planes", "Flugzeuge", "Avions", "Aviones"},
  {"Display", "Anzeige", "Affichage", "Pantalla"},
  {"Calculator", "Rechner", "Calculatrice", "Calculadora"},
  {"Weather Icons", "Wettersymbole", "Icônes météo", "Iconos del clima"},
  {"Settings", "Einstellungen", "Paramètres", "Ajustes"},
  {"Save", "Speichern", "Enregistrer", "Guardar"},
  {"Refresh", "Aktualisieren", "Actualiser", "Actualizar"},
  {"Language", "Sprache", "Langue", "Idioma"},
  {"WiFi", "WLAN", "WiFi", "WiFi"},
  {"Password", "Passwort", "Mot de passe", "Contraseña"},
  {"SSID", "SSID", "SSID", "SSID"},
  {"Enter SSID", "SSID eingeben", "Saisir SSID", "Ingresar SSID"},
  {"Enter password", "Passwort eingeben", "Saisir mot de passe", "Ingresar contraseña"},
  {"Region", "Region", "Région", "Región"},
  {"Timezone", "Zeitzone", "Fuseau horaire", "Zona horaria"},
  {"24-hour format", "24-Stunden-Format", "Format 24 h", "Formato 24 horas"},
  {"Latitude", "Breitengrad", "Latitude", "Latitud"},
  {"Longitude", "Längengrad", "Longitude", "Longitud"},
  {"Guess from IP", "Aus IP ermitteln", "Déduire via IP", "Detectar por IP"},
  {"Guessing location...", "Standort wird ermittelt...", "Recherche de position...", "Detectando ubicación..."},
  {"Location guess failed", "Standortermittlung fehlgeschlagen", "Échec de géolocalisation", "Falló detección de ubicación"},
  {"Guessed: ", "Ermittelt: ", "Trouvé : ", "Detectado: "},
  {"unknown location", "unbekannter Ort", "lieu inconnu", "ubicación desconocida"},
  {"Metric (C)", "Metrisch (C)", "Métrique (C)", "Métrico (C)"},
  {"Center Latitude", "Mittelpunkt Breite", "Latitude centre", "Latitud centro"},
  {"Center Longitude", "Mittelpunkt Länge", "Longitude centre", "Longitud centro"},
  {"Visibility Range (km)", "Sichtweite (km)", "Portée de visibilité (km)", "Alcance de visibilidad (km)"},
  {"Used to compute OpenSky bounding box.", "Wird zur Berechnung der OpenSky-Grenzen verwendet.", "Utilisé pour calculer la zone OpenSky.", "Se usa para calcular el área de OpenSky."},
  {"Metric units", "Metrische Einheiten", "Unités métriques", "Unidades métricas"},
  {"Hide planes on ground", "Flugzeuge am Boden ausblenden", "Masquer les avions au sol", "Ocultar aviones en tierra"},
  {"Brightness", "Helligkeit", "Luminosité", "Brillo"},
  {"Screen Rotation", "Bildschirmdrehung", "Rotation écran", "Rotación de pantalla"},
  {"0° (Portrait)\n90° (Landscape)\n180° (Portrait Inverted)\n270° (Landscape Inverted)", "0° (Hochformat)\n90° (Querformat)\n180° (Hochformat gedreht)\n270° (Querformat gedreht)", "0° (Portrait)\n90° (Paysage)\n180° (Portrait inversé)\n270° (Paysage inversé)", "0° (Vertical)\n90° (Horizontal)\n180° (Vertical invertido)\n270° (Horizontal invertido)"},
  {"Time", "Zeit", "Heure", "Hora"},
  {"Weather", "Wetter", "Météo", "Tiempo"},
  {"Planes", "Flugzeuge", "Avions", "Aviones"},
  {"Display", "Anzeige", "Affichage", "Pantalla"},
  {"No weather data", "Keine Wetterdaten", "Aucune donnée météo", "Sin datos del clima"},
  {"Refreshing...", "Aktualisiere...", "Actualisation...", "Actualizando..."},
  {"Wind", "Wind", "Vent", "Viento"},
  {"Configure center/range in Settings > Planes", "Mittelpunkt/Reichweite in Einstellungen > Flugzeuge setzen", "Configurer centre/portée dans Paramètres > Avions", "Configura centro/rango en Ajustes > Aviones"},
  {"Planes: --", "Flugzeuge: --", "Avions : --", "Aviones: --"},
  {"No plane data yet", "Noch keine Flugdaten", "Pas encore de données avion", "Aún no hay datos de aviones"},
  {"Center %.3f, %.3f | Range %.0f km", "Zentrum %.3f, %.3f | Reichweite %.0f km", "Centre %.3f, %.3f | Portée %.0f km", "Centro %.3f, %.3f | Rango %.0f km"},
  {"Planes: %d  Cr:%d", "Flugzeuge: %d  Cr:%d", "Avions : %d  Cr:%d", "Aviones: %d  Cr:%d"},
  {"Planes: 0", "Flugzeuge: 0", "Avions : 0", "Aviones: 0"},
  {"Planes: %d", "Flugzeuge: %d", "Avions : %d", "Aviones: %d"},
  {"On Ground", "Am Boden", "Au sol", "En tierra"},
  {"Alt: %.0fm  %.0f km/h  %.1f km", "Höhe: %.0fm  %.0f km/h  %.1f km", "Alt : %.0fm  %.0f km/h  %.1f km", "Alt: %.0fm  %.0f km/h  %.1f km"},
  {"Alt: %.0fft  %.0f mph  %.1f mi", "Höhe: %.0fft  %.0f mph  %.1f mi", "Alt : %.0f ft  %.0f mph  %.1f mi", "Alt: %.0f ft  %.0f mph  %.1f mi"},
  {"Error", "Fehler", "Erreur", "Error"},
  {"Clear", "Klar", "Dégagé", "Despejado"},
  {"Cloudy", "Bewölkt", "Nuageux", "Nublado"},
  {"Fog", "Nebel", "Brouillard", "Niebla"},
  {"Rain", "Regen", "Pluie", "Lluvia"},
  {"Snow", "Schnee", "Neige", "Nieve"},
  {"Showers", "Schauer", "Averses", "Chubascos"},
  {"Thunderstorm", "Gewitter", "Orage", "Tormenta"},
  {"Unknown", "Unbekannt", "Inconnu", "Desconocido"},
  {"Sun", "So", "Dim", "Dom"},
  {"Mon", "Mo", "Lun", "Lun"},
  {"Tue", "Di", "Mar", "Mar"},
  {"Wed", "Mi", "Mer", "Mié"},
  {"Thu", "Do", "Jeu", "Jue"},
  {"Fri", "Fr", "Ven", "Vie"},
  {"Sat", "Sa", "Sam", "Sáb"},
};

void load_from_config()
{
  ConfigStoreService &cfg = ConfigStoreService::instance();
  if (!cfg.load("ui")) return;
  const int stored = cfg.get_int("language", 0);
  if (stored >= 0 && stored < (int)Language::Count)
    g_language = (Language)stored;
}

Language language() { return g_language; }

void set_language(Language lang, bool persist)
{
  g_language = lang;
  if (!persist) return;
  ConfigStoreService &cfg = ConfigStoreService::instance();
  if (cfg.load("ui")) {
    cfg.set("language", (int)lang);
    cfg.save("ui");
  }
}

const char *tr(TextId id)
{
  return kTranslations[(int)id][(int)g_language];
}

const char *translate_app_name(const char *default_name)
{
  if (!default_name) return "";
  if (std::strcmp(default_name, "Settings") == 0) return tr(TextId::AppSettings);
  if (std::strcmp(default_name, "Time") == 0) return tr(TextId::AppTime);
  if (std::strcmp(default_name, "Weather") == 0) return tr(TextId::AppWeather);
  if (std::strcmp(default_name, "Planes") == 0) return tr(TextId::AppPlanes);
  if (std::strcmp(default_name, "Display") == 0) return tr(TextId::AppDisplay);
  if (std::strcmp(default_name, "Calculator") == 0) return tr(TextId::AppCalculator);
  if (std::strcmp(default_name, "Weather Icons") == 0) return tr(TextId::AppWeatherIcons);
  return default_name;
}

const char *weekday_short(int day_index)
{
  static constexpr TextId days[] = {
    TextId::WeekdaySun, TextId::WeekdayMon, TextId::WeekdayTue, TextId::WeekdayWed,
    TextId::WeekdayThu, TextId::WeekdayFri, TextId::WeekdaySat,
  };
  if (day_index < 0 || day_index > 6) return "--";
  return tr(days[day_index]);
}

}  // namespace I18n
