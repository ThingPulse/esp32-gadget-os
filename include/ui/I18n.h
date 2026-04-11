#pragma once

#include <cstdint>

namespace I18n {

enum class Language : uint8_t {
  English = 0,
  German,
  French,
  Spanish,
  Count
};

enum class TextId : uint16_t {
  AppSettings,
  AppTime,
  AppWeather,
  AppPlanes,
  AppDisplay,
  AppCalculator,
  AppWeatherIcons,
  Settings,
  Save,
  Refresh,
  Language,
  WiFi,
  Password,
  Ssid,
  EnterSsid,
  EnterPassword,
  Region,
  Timezone,
  Hour24Format,
  Latitude,
  Longitude,
  GuessFromIp,
  GuessingLocation,
  LocationGuessFailed,
  GuessedPrefix,
  UnknownLocation,
  MetricC,
  CenterLatitude,
  CenterLongitude,
  VisibilityRangeKm,
  OpenSkyHint,
  MetricUnits,
  HidePlanesOnGround,
  Brightness,
  ScreenRotation,
  RotationOptions,
  Time,
  Weather,
  Planes,
  Display,
  NoWeatherData,
  Refreshing,
  Wind,
  ConfigurePlanesHint,
  PlanesCountUnknown,
  NoPlaneDataYet,
  CenterRangeFmt,
  PlanesCountFmt,
  PlanesZeroFmt,
  PlanesCountOnlyFmt,
  OnGround,
  AltMetricFmt,
  AltImperialFmt,
  CalculatorError,
  Clear,
  Cloudy,
  Fog,
  Rain,
  Snow,
  Showers,
  Thunderstorm,
  Unknown,
  WeekdaySun,
  WeekdayMon,
  WeekdayTue,
  WeekdayWed,
  WeekdayThu,
  WeekdayFri,
  WeekdaySat,
  Count
};

void load_from_config();
Language language();
void set_language(Language lang, bool persist);
const char *tr(TextId id);
const char *translate_app_name(const char *default_name);
const char *weekday_short(int day_index);

}  // namespace I18n
