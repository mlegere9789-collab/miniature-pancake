/**
 * Weather integration for environmental-fit scoring and the frost-risk
 * dashboard banner (spec §4.3, §4.5, §5). Uses Open-Meteo's free forecast
 * API (no API key required) rather than NOAA/OpenWeather directly, so this
 * works out of the box in dev; swap the fetch below for NOAA/OpenWeather if
 * the product needs their specific station coverage or alerts later.
 */

export interface WeatherSignals {
  frostRiskTonight: boolean;
  minTempTonightC: number;
  droughtStressDetected: boolean;
  precipLast7DaysMm: number;
}

const OPEN_METEO_URL = "https://api.open-meteo.com/v1/forecast";
const FROST_THRESHOLD_C = 2;
const DROUGHT_THRESHOLD_MM = 5;

export async function fetchWeatherSignals(latitude: number, longitude: number): Promise<WeatherSignals> {
  const params = new URLSearchParams({
    latitude: String(latitude),
    longitude: String(longitude),
    daily: "temperature_2m_min,precipitation_sum",
    past_days: "7",
    forecast_days: "1",
    timezone: "auto",
  });

  const res = await fetch(`${OPEN_METEO_URL}?${params.toString()}`);
  if (!res.ok) {
    throw new Error(`weather lookup failed (${res.status})`);
  }

  const data = (await res.json()) as {
    daily: { time: string[]; temperature_2m_min: number[]; precipitation_sum: number[] };
  };

  const days = data.daily.time.length;
  const todayIndex = days - 1; // forecast_days=1 appends today/tonight after the 7 past days
  const minTempTonightC = data.daily.temperature_2m_min[todayIndex];

  const past7Precip = data.daily.precipitation_sum.slice(0, days - 1);
  const precipLast7DaysMm = past7Precip.reduce((sum, mm) => sum + mm, 0);

  return {
    frostRiskTonight: minTempTonightC <= FROST_THRESHOLD_C,
    minTempTonightC,
    droughtStressDetected: precipLast7DaysMm < DROUGHT_THRESHOLD_MM,
    precipLast7DaysMm,
  };
}

/**
 * Environmental-fit penalty applied on top of the diagnostic engine's own
 * light-exposure estimate (spec §3.1.3: mismatch drags score down even
 * without visible symptoms yet).
 */
export function applyWeatherPenalty(baseScore: number, weather: WeatherSignals, plantIsFrostSensitive: boolean): number {
  let penalty = 0;
  if (weather.frostRiskTonight && plantIsFrostSensitive) penalty += 20;
  if (weather.droughtStressDetected) penalty += 10;
  return Math.max(0, Math.min(100, baseScore - penalty));
}
