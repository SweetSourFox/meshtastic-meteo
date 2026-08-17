# Meteo on Meshtastic — device validation

Run on Cardputer-Adv + Grove ENV Pro + CO2L + external GPS (Cap UART).

| # | Scenario | Pass |
|---|----------|------|
| 1 | GPS fix ? Meteo FORECAST shows time, no `NO RTC` | |
| 2 | Run ?3h, power off/on ? SLP ring restored, trend not `COLLECTING` | |
| 3 | Settings survive reboot (NVS `meteo`) | |
| 4 | Press `L` with logging enabled ? `/meteo/logs/YYYY-MM-DD.csv` | |
| 5 | PaHUB channels 0–5 find BME/SCD | |
| 6 | All 6 nav pages + SETTINGS + 4 themes navigable | |
| 7 | LoRa mesh still sends/receives while Meteo polling | |

## Flash

```bash
pio run -e m5stack-cardputer-adv -t upload --upload-port COMx
```
