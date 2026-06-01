#pragma once

// === EBLA ADDITION (Layer 3) - explicit stdlib includes for the strike counter ===
#include <chrono>
#include <cstdint>
#include <shared_mutex>
#include <unordered_map>
// === END EBLA ADDITION ===

#include "common/util.hpp"
#include "config/config.hpp"
#include "libp2p/Common.h"
#include "libp2p/Host.h"
#include "network/eblacap/ebla_peer.hpp"
#include "network/eblacap/packet_types.hpp"
#include "network/eblacap/stats/time_period_packets_stats.hpp"

namespace ebla {
class PbftManager;
}

namespace ebla::network::eblacap {

/**
 * @brief PeersState contains common members and functions related to peers, host, etc... that are shared among multiple
 * classes
 */
class PeersState {
 public:
  using PeersMap = std::unordered_map<dev::p2p::NodeID, std::shared_ptr<EblaPeer>>;

  PeersState(std::weak_ptr<dev::p2p::Host> host, const FullNodeConfig& conf);

  std::shared_ptr<EblaPeer> getPeer(const dev::p2p::NodeID& node_id) const;
  std::shared_ptr<EblaPeer> getPendingPeer(const dev::p2p::NodeID& node_id) const;

  /**
   * @brief Get known peer based on packet sender and packet type. For StatusPacket peer can be obtained from
   *        pending_peers, for all other packet types peer can be obtained only from peers map, in which are only
   *        peers that already sent initial StatusPacket
   *
   * @return <std::shared_ptr<EblaPeer>, ""> if packet sender is known peer, otherwise <nullptr, "err message">
   */
  std::pair<std::shared_ptr<EblaPeer>, std::string> getPacketSenderPeer(const dev::p2p::NodeID& node_id,
                                                                        SubprotocolPacketType packet_type) const;

  PeersMap getAllPeers() const;
  std::vector<dev::p2p::NodeID> getAllPendingPeersIDs() const;
  size_t getPeersCount() const;
  std::shared_ptr<EblaPeer> addPendingPeer(const dev::p2p::NodeID& node_id, const std::string& address);
  void erasePeer(const dev::p2p::NodeID& node_id);
  std::shared_ptr<EblaPeer> setPeerAsReadyToSendMessages(dev::p2p::NodeID const& node_id,
                                                         std::shared_ptr<EblaPeer> peer);

  /**
   * @brief Marks peer as malicious
   * @param peer_id
   */
  void set_peer_malicious(const dev::p2p::NodeID& peer_id);

  /**
   * @brief Checks if peer is in malicious peers list
   * @return returns true if peer is in malicious peer list
   */
  bool is_peer_malicious(const dev::p2p::NodeID& peer_id);

  // === EBLA ADDITION (Layer 3 - per-peer VRF-failure strike counter) ===
  /**
   * @brief Record a VRF/VDF verification failure originating from a peer.
   *
   * Maintains a sliding window of strikes per peer. Returns true when the
   * peer has reached kVrfFailureStrikeLimit strikes within
   * kVrfFailureWindowSeconds; the caller is then expected to throw
   * MaliciousPeerException so the existing escalation in
   * PacketHandler::process_ runs (set_peer_malicious + disconnectPeer).
   *
   * Honors kConf.network.disable_peer_blacklist - when set, the strike
   * counter is bypassed and this method returns false unconditionally.
   *
   * @param peer_id  the peer whose strike to record
   * @return true if peer has reached the strike threshold (caller must
   *         throw MaliciousPeerException); false otherwise
   */
  bool record_vrf_failure_strike(const dev::p2p::NodeID& peer_id);
  // === END EBLA ADDITION ===

  /**
   * @brief Handle malicious peer
   * @param id
   */
  void handleMaliciousSyncPeer(const dev::p2p::NodeID& id);

  /**
   * @param filter_func
   * @return EblaPeer shared_ptr with max chain size
   */
  std::shared_ptr<EblaPeer> getMaxChainPeer(
      const std::shared_ptr<PbftManager> pbft_mgr, std::function<bool(const std::shared_ptr<EblaPeer>&)> filter_func =
                                                       [](const std::shared_ptr<EblaPeer>&) { return true; });

 private:
  /**
   * @brief Disconnect peer
   * @param id
   */
  void disconnectPeer(const dev::p2p::NodeID& id);

 public:
  const std::weak_ptr<dev::p2p::Host> host_;

 private:
  mutable std::shared_mutex peers_mutex_;
  PeersMap peers_;
  PeersMap pending_peers_;

  ThreadSafeMap<dev::p2p::NodeID, std::chrono::steady_clock::time_point> malicious_peers_;
  // === EBLA ADDITION (Layer 3 - VRF-failure strike counter) ===
  // Strike window and threshold are intentionally NOT in genesis JSONs
  // (this is local anti-spam tuning, not consensus). Per the §7.4 BUG-31
  // lesson, each additional duplication site is one more drift surface.
  static constexpr std::chrono::seconds kVrfFailureWindowSeconds{600};
  static constexpr uint32_t kVrfFailureStrikeLimit = 3;

  struct VrfStrikeRecord {
    uint32_t count = 0;
    std::chrono::steady_clock::time_point first_in_window{};
  };

  // Dedicated shared_mutex (not ThreadSafeMap) because record-strike is a
  // "read entry, mutate, write back" operation that ThreadSafeMap's
  // value-returning get() cannot do atomically.
  mutable std::shared_mutex strikes_mutex_;
  std::unordered_map<dev::p2p::NodeID, VrfStrikeRecord> vrf_failure_strikes_;

  // Layer 3 v1.1: rate-limit the lazy-prune sweep. Without this, an attacker
  // rotating NodeIDs at high rate inflates the map; each call walks the
  // whole map, total work scales as O(N^2) over N unique attacker NodeIDs.
  // With this, the sweep runs at most once per kStrikesPruneIntervalSeconds,
  // reducing worst-case to O(N) per minute regardless of strike rate.
  static constexpr std::chrono::seconds kStrikesPruneIntervalSeconds{60};
  std::chrono::steady_clock::time_point last_strikes_prune_time_{};
  // === END EBLA ADDITION ===
  const FullNodeConfig kConf;
};

}  // namespace ebla::network::eblacap
