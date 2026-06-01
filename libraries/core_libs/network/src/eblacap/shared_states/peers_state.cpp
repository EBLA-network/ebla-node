#include "network/eblacap/shared_states/peers_state.hpp"

#include "pbft/pbft_manager.hpp"

namespace ebla::network::eblacap {

PeersState::PeersState(std::weak_ptr<dev::p2p::Host> host, const FullNodeConfig& conf)
    : host_(std::move(host)), kConf(conf) {}

std::shared_ptr<EblaPeer> PeersState::getPeer(const dev::p2p::NodeID& node_id) const {
  std::shared_lock lock(peers_mutex_);

  auto it_peer = peers_.find(node_id);
  if (it_peer != peers_.end()) {
    return it_peer->second;
  }

  return nullptr;
}

std::shared_ptr<EblaPeer> PeersState::getPendingPeer(const dev::p2p::NodeID& node_id) const {
  std::shared_lock lock(peers_mutex_);

  auto it_peer = pending_peers_.find(node_id);
  if (it_peer != pending_peers_.end()) {
    return it_peer->second;
  }

  return nullptr;
}

std::pair<std::shared_ptr<EblaPeer>, std::string> PeersState::getPacketSenderPeer(
    const dev::p2p::NodeID& node_id, SubprotocolPacketType packet_type) const {
  std::shared_lock lock(peers_mutex_);

  // If peer is in peers_, it means he already sent us initial status packet and
  // we can receive/send any kind of packet from/to him
  if (const auto it_peer = peers_.find(node_id); it_peer != peers_.end()) {
    return {it_peer->second, ""};
  }

  // If peer is in pending_peers_, it means he has not yet sent us initial status packet and
  // we can receive/send only StatusPacket from/to him
  if (const auto it_peer = pending_peers_.find(node_id); it_peer != pending_peers_.end()) {
    if (packet_type == SubprotocolPacketType::kStatusPacket) {
      return {it_peer->second, ""};
    } else {
      std::ostringstream error;
      error << "Peer " << node_id.abridged()
            << " is only in pending peers - probably did not send initial status packet yet";
      return {nullptr, error.str()};
    }
  }

  std::ostringstream error;
  error << "Peer " << node_id.abridged() << " is not in peers map anymore - probably lost connection";
  return {nullptr, error.str()};
}

std::vector<dev::p2p::NodeID> PeersState::getAllPendingPeersIDs() const {
  std::vector<dev::p2p::NodeID> peers;

  std::shared_lock lock(peers_mutex_);
  peers.reserve(pending_peers_.size());
  std::transform(pending_peers_.begin(), pending_peers_.end(), std::back_inserter(peers),
                 [](std::pair<const dev::p2p::NodeID, std::shared_ptr<EblaPeer>> const& peer) { return peer.first; });

  return peers;
}

PeersState::PeersMap PeersState::getAllPeers() const {
  std::shared_lock lock(peers_mutex_);
  return peers_;
}

std::shared_ptr<EblaPeer> PeersState::addPendingPeer(const dev::p2p::NodeID& node_id, const std::string& address) {
  std::unique_lock lock(peers_mutex_);
  auto ret =
      pending_peers_.emplace(node_id, std::make_shared<EblaPeer>(node_id, kConf.transactions_pool_size, address));
  if (!ret.second) {
    // LOG(log_er_) << "Peer " << node_id.abridged() << " is already in pending peers list";
  }

  return ret.first->second;
}

size_t PeersState::getPeersCount() const {
  std::shared_lock lock(peers_mutex_);

  return peers_.size();
}

void PeersState::erasePeer(dev::p2p::NodeID const& node_id) {
  std::unique_lock lock(peers_mutex_);
  pending_peers_.erase(node_id);
  peers_.erase(node_id);
}

std::shared_ptr<EblaPeer> PeersState::setPeerAsReadyToSendMessages(dev::p2p::NodeID const& node_id,
                                                                   std::shared_ptr<EblaPeer> peer) {
  std::unique_lock lock(peers_mutex_);
  pending_peers_.erase(node_id);
  auto ret = peers_.emplace(node_id, std::move(peer));
  if (!ret.second) {
    // LOG(log_er_) << "Peer " << node_id.abridged() << " is already in peers list";
  }

  return ret.first->second;
}

void PeersState::set_peer_malicious(const dev::p2p::NodeID& peer_id) {
  malicious_peers_.emplace(peer_id, std::chrono::steady_clock::now());
}

bool PeersState::is_peer_malicious(const dev::p2p::NodeID& peer_id) {
  if (kConf.network.disable_peer_blacklist) {
    return false;
  }

  // Peers are marked malicious for the time defined in conf_.peer_blacklist_timeout
  if (auto i = malicious_peers_.get(peer_id); i.second) {
    if (kConf.network.peer_blacklist_timeout == 0 ||
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - i.first).count() <=
            kConf.network.peer_blacklist_timeout) {
      return true;
    } else {
      malicious_peers_.erase(peer_id);
    }
  }

  // Delete any expired item from the list
  if (kConf.network.peer_blacklist_timeout > 0) {
    malicious_peers_.erase([this](const std::chrono::steady_clock::time_point& value) {
      return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - value).count() >
             kConf.network.peer_blacklist_timeout;
    });
  }

  return false;
}

// === EBLA ADDITION (Layer 3 - VRF-failure strike counter) ===
bool PeersState::record_vrf_failure_strike(const dev::p2p::NodeID& peer_id) {
  // Honour the same operator escape hatch is_peer_malicious uses.
  if (kConf.network.disable_peer_blacklist) {
    return false;
  }

  const auto now = std::chrono::steady_clock::now();
  std::unique_lock<std::shared_mutex> lock(strikes_mutex_);

  // Lazy prune (rate-limited): drop entries whose window expired, but only
  // run the sweep at most once every kStrikesPruneIntervalSeconds. Without
  // the rate-limit, an attacker rotating NodeIDs would force an O(N) walk
  // on every call, scaling total work as O(N^2). With the rate-limit, total
  // work is O(N) per minute regardless of strike rate.
  //
  // Note: stale entries may persist up to kStrikesPruneIntervalSeconds past
  // their nominal expiry (60s slop on a 600s window = 10%). Acceptable —
  // these entries cost ~32 bytes each and have no functional effect on
  // correctness; they only prevent a fresh strike from re-using the slot.
  if (std::chrono::duration_cast<std::chrono::seconds>(now - last_strikes_prune_time_) >=
      kStrikesPruneIntervalSeconds) {
    for (auto it = vrf_failure_strikes_.begin(); it != vrf_failure_strikes_.end();) {
      if (std::chrono::duration_cast<std::chrono::seconds>(now - it->second.first_in_window) >
          kVrfFailureWindowSeconds) {
        it = vrf_failure_strikes_.erase(it);
      } else {
        ++it;
      }
    }
    last_strikes_prune_time_ = now;
  }

  // Record this peer's strike.
  auto existing_it = vrf_failure_strikes_.find(peer_id);
  if (existing_it == vrf_failure_strikes_.end()) {
    // First strike (or freshly pruned).
    vrf_failure_strikes_[peer_id] = VrfStrikeRecord{1u, now};
    return false;
  }

  // Within window: increment.
  existing_it->second.count += 1u;
  return existing_it->second.count >= kVrfFailureStrikeLimit;
}
// === END EBLA ADDITION ===

void PeersState::handleMaliciousSyncPeer(const dev::p2p::NodeID& id) {
  set_peer_malicious(id);
  disconnectPeer(id);
}

void PeersState::disconnectPeer(const dev::p2p::NodeID& id) {
  if (auto host = host_.lock(); host) {
    host->disconnect(id, dev::p2p::UserReason);
  }
}

std::shared_ptr<EblaPeer> PeersState::getMaxChainPeer(
    const std::shared_ptr<PbftManager> pbft_mgr, std::function<bool(const std::shared_ptr<EblaPeer>&)> filter_func) {
  std::shared_ptr<EblaPeer> max_pbft_chain_peer;
  PbftPeriod max_pbft_chain_size = 0;
  uint64_t max_node_dag_level = 0;

  // Find peer with max pbft chain and dag level
  for (auto const& peer : getAllPeers()) {
    // Apply the filter function
    if (!filter_func(peer.second)) {
      continue;
    }

    if (peer.second->pbft_chain_size_ > max_pbft_chain_size) {
      if (peer.second->peer_light_node &&
          pbft_mgr->pbftSyncingPeriod() + peer.second->peer_light_node_history < peer.second->pbft_chain_size_) {
        // TODO: do we neet this log ???
        //        LOG(this->log_er_) << "Disconnecting from light node peer " << peer.first
        //                           << " History: " << peer.second->peer_light_node_history
        //                           << " chain size: " << peer.second->pbft_chain_size_;
        disconnectPeer(peer.first);
        continue;
      }

      max_pbft_chain_size = peer.second->pbft_chain_size_;
      max_node_dag_level = peer.second->dag_level_;
      max_pbft_chain_peer = peer.second;
    } else if (peer.second->pbft_chain_size_ == max_pbft_chain_size && peer.second->dag_level_ > max_node_dag_level) {
      max_node_dag_level = peer.second->dag_level_;
      max_pbft_chain_peer = peer.second;
    }
  }

  return max_pbft_chain_peer;
}

}  // namespace ebla::network::eblacap