# Firmware

Put your Arduino sketch (.ino) here.

Before committing, remove or replace:
1. WiFi SSID and password
2. The Google Apps Script `/exec` URL (anyone with it can write to your sheet)

Keep them in a `secrets.h` file that is listed in `.gitignore`, and commit a `secrets.example.h` with placeholders.

Notes: use the `/exec` URL (not `/echo`) with `HTTPC_STRICT_FOLLOW_REDIRECTS`; upload at 115200 baud if you get "No serial data received"; check the return value of `sgp.begin()` and `IAQmeasure()`.
