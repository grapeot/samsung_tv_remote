// secrets.example.h — template. Copy to secrets.h and fill in.
//   cp src/samsung_tv_remote/secrets.example.h src/samsung_tv_remote/secrets.h
#pragma once

#define WIFI_SSID           "your-wifi-ssid"
#define WIFI_PASS           "your-wifi-password"

// TV local IP on the same LAN. Find with: dns-sd -B _samsungmsf._tcp local
// The WS token is obtained automatically via first-run pairing (stored in NVS).
#define TV_HOST             "192.168.x.x"