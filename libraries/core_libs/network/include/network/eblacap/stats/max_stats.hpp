#pragma once

#include <json/value.h>

#include "network/eblacap/stats/packet_stats.hpp"

namespace ebla::network::eblacap {

/**
 * @brief MAx stats data holder class
 */
class MaxStats {
 public:
  /**
   * @return max stats json
   */
  Json::Value getMaxStatsJson() const;

  /**
   * @brief Updates max states based on provided packet_stats
   *
   * @param packet_stats
   */
  void updateMaxStats(const PacketStats& packet_stats);

 private:
  PacketStats max_count_stats_;
  PacketStats max_size_stats_;
  PacketStats max_processing_duration_stats_;
  PacketStats max_tp_wait_time_stats_;
};

}  // namespace ebla::network::eblacap