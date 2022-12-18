//
// Created by consti10 on 17.05.22.
//

#ifndef OPENHD_OPENHD_OHD_INTERFACE_SRC_WIFIHOTSPOT_H_
#define OPENHD_OPENHD_OHD_INTERFACE_SRC_WIFIHOTSPOT_H_

#include <string>
#include <vector>

#include "wifi_card.hpp"
#include "wifi_hotspot_settings.h"
#include "mavlink_settings/ISettingsComponent.hpp"

/**
 * Wifi hotspot refers to creating a WiFi Access point on the device we are running on.
 * External clients like QOpenHD running on a tablet can then connect to the hotspot.
 * Note that video and telemetry has to be forwarded to clients connected to the wifi hotspot.
 * To get those clients, you can register a callback here (uninmplemented r.n, TODO)
 * Change Nov4 2022: Uses network manager - we already have network manager installed and enabled by default on the rpi on the openhd images,
 * but the default raspbian images from pi foundation have it only installed, but disabled by default (they'l use it eventually)
 *
 * NOTE: This is a class with self-contained settings to not interfer with wifibroadcast or similar.
 */
class WifiHotspot {
 public:
  /**
   * Utility for starting, stopping WIFI AP (Hotspot) and forwarding the client connect/disconnect events.
   */
  explicit WifiHotspot(WiFiCard wifiCard);
  /**
   * Expose all hotspot settings such that they can be changed via mavlink
   */
  std::vector<openhd::Setting> get_all_settings();
 private:
  // NOTE: might block, use async
  // just runs the appropriate network manager (nmcli) command to start wifi hotspot
  void start();
  // NOTE: might block,use async
  // just runs the appropriate network manager (nmcli) command to stop an already created hotspot
  void stop();
  void start_async();
  void stop_async();
  // Ip addresses of all connected clients.
  // A client might dynamically connect or disconnect from the AP at run time,
  // In this case the apropriate callbacks have to be called.
  std::vector<std::string> connectedClientsIps;
  const WiFiCard m_wifi_card;
  bool started=false;
  std::unique_ptr<WifiHotspotSettingsHolder> m_settings;
};

#endif //OPENHD_OPENHD_OHD_INTERFACE_SRC_WIFIHOTSPOT_H_
