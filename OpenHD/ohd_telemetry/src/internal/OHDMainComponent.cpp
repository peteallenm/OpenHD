//
// Created by consti10 on 19.04.22.
//

#include <iostream>

#include "OHDMainComponent.h"
#include "OnboardComputerStatus.hpp"
#include "RebootUtil.hpp"

OHDMainComponent::OHDMainComponent(
    OHDPlatform platform1,uint8_t parent_sys_id,
    bool runsOnAir) : platform(platform1),RUNS_ON_AIR(runsOnAir),MavlinkComponent(parent_sys_id,MAV_COMP_ID_ONBOARD_COMPUTER) {
  logMessagesReceiver =
      std::make_unique<SocketHelper::UDPReceiver>(SocketHelper::ADDRESS_LOCALHOST,
                                                  OHD_LOCAL_LOG_MESSAGES_UDP_PORT,
                                                  [this](const uint8_t *payload,
                                                         const std::size_t payloadSize) {
                                                    this->_status_text_accumulator.processLogMessageData(payload, payloadSize);
                                                  });
  logMessagesReceiver->runInBackground();
}

std::vector<MavlinkMessage> OHDMainComponent::generate_mavlink_messages() {
  //std::cout<<"InternalTelemetry::generate_mavlink_messages()\n";
  std::vector<MavlinkMessage> ret;
  ret.push_back(MavlinkComponent::create_heartbeat());
  const bool is_platform_rpi=platform.platform_type==PlatformType::RaspberryPi;
  ret.push_back(OnboardComputerStatus::createOnboardComputerStatus(_sys_id,_comp_id,is_platform_rpi));
  MavlinkComponent::vec_append(ret,generateWifibroadcastStatistics());
  //ret.push_back(generateOpenHDVersion());
  // TODO remove for release
  //ret.push_back(MExampleMessage::position(mSysId,mCompId));
  // TODO remove for release
  //_status_text_accumulator.manually_add_message(RUNS_ON_AIR ? "HelloAir" : "HelloGround");
  const auto logs = generateLogMessages();
  MavlinkComponent::vec_append(ret,logs);
  //ret.insert(ret.end(), logs.begin(), logs.end());
  return ret;
}

std::vector<MavlinkMessage> OHDMainComponent::process_mavlink_message(const MavlinkMessage &msg) {
  std::vector<MavlinkMessage> ret{};
  switch (msg.m.msgid) { // NOLINT(cppcoreguidelines-narrowing-conversions)
    case MAVLINK_MSG_ID_PING:{
      // We respond to ping messages
      auto response=handlePingMessage(msg);
      if(response.has_value()){
        ret.push_back(response.value());
      }
    }break;
    case MAVLINK_MSG_ID_COMMAND_LONG:{
      mavlink_command_long_t command;
      mavlink_msg_command_long_decode(&msg.m,&command);
      std::cout<<"Got MAVLINK_MSG_ID_COMMAND_LONG:"<<command.command<<" "<<static_cast<uint32_t>(command.param1)<<"\n";
      if(command.command==MAV_CMD_PREFLIGHT_REBOOT_SHUTDOWN){
        //https://mavlink.io/en/messages/common.html#MAV_CMD_PREFLIGHT_REBOOT_SHUTDOWN
        std::cout<<"Got shutdown command";
        RebootUtil::handlePowerCommand(false);
      }else if(command.command==MAV_CMD_REQUEST_MESSAGE){
        const auto requested_message_id=static_cast<uint32_t>(command.param1);
        std::cout<<"Someone requested a specific message: "<<requested_message_id<<"\n";
        if(requested_message_id==MAVLINK_MSG_ID_OPENHD_VERSION_MESSAGE){
          std::cout<<"Sent OpenHD version\n";
          ret.push_back(generateOpenHDVersion());
        }
      }
      // TODO have an ack response.
    }break;
    default:
      break;
  }
  return ret;
}

std::vector<MavlinkMessage> OHDMainComponent::generateWifibroadcastStatistics(){
  /*OpenHDStatisticsWriter::Data data;
  // for now, write some crap
  data.radio_port = 0;
  data.count_p_all = 3;
  data.count_p_dec_err = 4;
  auto msg = WBStatisticsConverter::convertWbStatisticsToMavlink(data,_sys_id,_comp_id);
  return msg;*/
  std::lock_guard<std::mutex> guard(_last_link_stats_mutex);
  std::vector<MavlinkMessage> ret;
  // stats for all the wifi card(s)
  for(int i=0;i<_last_link_stats.stats_all_cards.size();i++){
	MavlinkMessage msg;
	const auto card_stats=_last_link_stats.stats_all_cards.at(i);
	if(!card_stats.exists_in_openhd){
	  // skip non active cards
	  continue;
	}
	mavlink_msg_openhd_wifi_card_pack(_sys_id,_comp_id,&msg.m,i,card_stats.rx_rssi,
									  card_stats.count_p_received,card_stats.count_p_injected,0,0);
	std::cout << card_stats.to_string(i)<<"\n";
	ret.push_back(msg);
  }
  {
	MavlinkMessage msg;
	const auto& all_stats=_last_link_stats.stats_total_all_streams;
	std::cout<<all_stats.to_string()<<"\n";
	mavlink_msg_openhd_stats_total_all_streams_pack(_sys_id,_comp_id,&msg.m,all_stats.count_wifi_packets_received,all_stats.count_bytes_received,
													all_stats.count_wifi_packets_injected,all_stats.count_bytes_injected);
	ret.push_back(msg);
  }
  // stats per ling
  //mavlink_msg_openhd_fec_link_rx_statistics_pack()
  return ret;
}

std::vector<MavlinkMessage> OHDMainComponent::generateLogMessages() {
  const auto messages=_status_text_accumulator.get_messages();
  std::vector<MavlinkMessage> ret;
  // limit to 5 to save bandwidth
  for(const auto& msg:messages){
    if (ret.size() < 5) {
      //std::cout<<"Msg:"<<msg.message<<"\n";
      MavlinkMessage mavMsg;
      StatusTextAccumulator::convert(mavMsg.m,msg,_sys_id,_comp_id);
      ret.push_back(mavMsg);
    } else {
      std::stringstream ss;
      ss << "Dropping log message " << msg.message << "\n";
      std::cout << ss.str();
    }
  }
  return ret;
}

MavlinkMessage OHDMainComponent::generateOpenHDVersion() const {
  MavlinkMessage msg;
  char bufferBigEnough[30]={};
  std::strncpy((char *)bufferBigEnough,OHD_VERSION_NUMBER_STRING,30);
  mavlink_msg_openhd_version_message_pack(_sys_id,_comp_id, &msg.m, bufferBigEnough);
  //mavlink_component_information_t x;
  return msg;
}

void OHDMainComponent::set_link_statistics(openhd::link_statistics::AllStats stats){
  //std::cout<<"OHDMainComponent::set_link_statistics\n";
  std::lock_guard<std::mutex> guard(_last_link_stats_mutex);
  _last_link_stats=stats;
}


