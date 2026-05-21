#include "config/config.hpp"

#include <json/json.h>

#include <fstream>

#include "common/config_exception.hpp"
#include "config/config_utils.hpp"

namespace ebla {

// EBLA DB_ROADMAP_v01 §2.2 / §2.6 / §4.4 — DBConfig parse helpers.
//
// validateArchivePath: enforces the security properties from Step 3.10.8 and
// Step 4.2:
//   - path is non-empty
//   - path length <= 1024 chars (defends against malformed JSON blobs)
//   - path is absolute (rejects "~/foo", "./foo", relative paths)
//   - path is rooted under one of: /mnt, /srv, /opt, /var/lib  (no /, /etc,
//     /usr, /boot, /home, etc. — operator can't accidentally point cold tier
//     at a system directory)
//   - symlink resolution is NOT performed here; phase 7.5 health-check does it
//
// Called from both dec_json(DBConfig&) below AND from the CLI override path
// in libraries/cli/src/config.cpp (which has its own copy with internal
// linkage — kept here as `static` for the same reason).
static void validateArchivePath(const std::filesystem::path &p) {
  const auto s = p.string();
  if (s.empty()) {
    throw ConfigException("db_archive_path is empty but db_tiering_enabled is true");
  }
  if (s.size() > 1024) {
    throw ConfigException("db_archive_path is too long (max 1024 chars)");
  }
  if (!p.is_absolute()) {
    throw ConfigException("db_archive_path must be absolute (got: " + s + ")");
  }
  static constexpr std::array<const char *, 4> kAllowedPrefixes = {"/mnt/", "/srv/", "/opt/", "/var/lib/"};
  bool ok = false;
  for (const auto *prefix : kAllowedPrefixes) {
    if (s.rfind(prefix, 0) == 0) {  // starts_with (C++17-compatible)
      ok = true;
      break;
    }
  }
  if (!ok) {
    throw ConfigException("db_archive_path must be under /mnt, /srv, /opt, or /var/lib (got: " + s + ")");
  }
}

// EBLA DB_ROADMAP_v01 §2.6 trap #1 — read uint64 with validate-before-cast,
// reject negatives explicitly. jsoncpp's asUInt64() on a negative value wraps
// silently; we want a hard reject with an actionable error message.
static uint64_t readPositiveUInt64(Json::Value const &json, const std::string &key, uint64_t fallback) {
  if (!json.isMember(key) || json[key].isNull()) {
    return fallback;
  }
  const auto &v = json[key];
  if (!v.isIntegral()) {
    throw ConfigException(key + " must be an integer");
  }
  if (v.isInt64() && v.asInt64() < 0) {
    throw ConfigException(key + " must be non-negative (got: " + std::to_string(v.asInt64()) + ")");
  }
  return v.asUInt64();
}

void dec_json(Json::Value const &json, DBConfig &db_config) {
  db_config.db_snapshot_each_n_pbft_block =
      getConfigDataAsUInt(json, {"db_snapshot_each_n_pbft_block"}, true, db_config.db_snapshot_each_n_pbft_block);

  db_config.db_max_snapshots = getConfigDataAsUInt(json, {"db_max_snapshots"}, true, db_config.db_max_snapshots);
  db_config.db_max_open_files = getConfigDataAsUInt(json, {"db_max_open_files"}, true, db_config.db_max_open_files);
  db_config.db_compression = getConfigDataAsBoolean(json, {"db_compression"}, true, db_config.db_compression);

  // EBLA DB_ROADMAP_v01 §2.2 — RAM-discipline tunables (Phase 5).
  // Upper bounds intentionally generous; lower bounds are operationally enforced.
  db_config.db_block_cache_size_bytes =
      readPositiveUInt64(json, "db_block_cache_size_bytes", db_config.db_block_cache_size_bytes);
  if (db_config.db_block_cache_size_bytes < (16ULL << 20)) {  // <16 MiB is useless
    throw ConfigException("db_block_cache_size_bytes must be at least 16 MiB");
  }
  if (db_config.db_block_cache_size_bytes > (1ULL << 40)) {  // >1 TiB is obviously misconfigured
    throw ConfigException("db_block_cache_size_bytes is unreasonably large (>1 TiB)");
  }

  db_config.db_write_buffer_size_bytes =
      readPositiveUInt64(json, "db_write_buffer_size_bytes", db_config.db_write_buffer_size_bytes);
  if (db_config.db_write_buffer_size_bytes < (256ULL << 20)) {  // mid-PBFT-round flush risk below this
    throw ConfigException("db_write_buffer_size_bytes must be at least 256 MiB (mid-round flush risk)");
  }
  if (db_config.db_write_buffer_size_bytes > (32ULL << 30)) {
    throw ConfigException("db_write_buffer_size_bytes is unreasonably large (>32 GiB)");
  }

  // EBLA DB_ROADMAP_v01 §2.2 / §6–§7 — Tiered storage (Phase 6-7).
  db_config.db_tiering_enabled =
      getConfigDataAsBoolean(json, {"db_tiering_enabled"}, true, db_config.db_tiering_enabled);

  if (json.isMember("db_archive_path") && !json["db_archive_path"].isNull()) {
    db_config.db_archive_path = std::filesystem::path(getConfigDataAsString(json, {"db_archive_path"}));
  }

  db_config.db_hot_size_limit_bytes =
      readPositiveUInt64(json, "db_hot_size_limit_bytes", db_config.db_hot_size_limit_bytes);

  // §2.6 trap #1 — read into uint32_t, validate range 1..22, THEN narrow to uint8_t.
  // This forecloses the silent-truncation class (e.g. JSON 277 → uint8_t 21 → passes
  // the post-cast range check despite being wrong).
  if (json.isMember("db_cold_compression_level") && !json["db_cold_compression_level"].isNull()) {
    const uint32_t lvl =
        getConfigDataAsUInt(json, {"db_cold_compression_level"}, true, db_config.db_cold_compression_level);
    if (lvl < 1 || lvl > 22) {
      throw ConfigException("db_cold_compression_level must be in range 1..22 (got: " + std::to_string(lvl) + ")");
    }
    db_config.db_cold_compression_level = static_cast<uint8_t>(lvl);
  }

  // Cross-field validation: tiering enabled implies archive path is set and valid,
  // and the hot-size limit is at least 10 GiB.
  if (db_config.db_tiering_enabled) {
    validateArchivePath(db_config.db_archive_path);
    if (db_config.db_hot_size_limit_bytes < (10ULL << 30)) {
      throw ConfigException("db_hot_size_limit_bytes must be >= 10 GiB when tiering is enabled");
    }
  }
}

std::vector<logger::Config> FullNodeConfig::loadLoggingConfigs(const Json::Value &logging) {
  // could be empty if config loaded from json e.g. tests
  if (!json_file_name.empty()) {
    last_json_update_time = std::filesystem::last_write_time(std::filesystem::path(json_file_name));
  }
  std::vector<logger::Config> res;
  if (!logging.isNull()) {
    if (auto path = getConfigData(logging, {"log_path"}, true); !path.isNull()) {
      log_path = path.asString();
    } else {
      log_path = data_path / "logs";
    }
    for (auto &item : logging["configurations"]) {
      auto on = getConfigDataAsBoolean(item, {"on"});
      if (on) {
        logger::Config logging;
        logging.name = getConfigDataAsString(item, {"name"});
        logging.verbosity = logger::stringToVerbosity(getConfigDataAsString(item, {"verbosity"}));
        for (auto &ch : item["channels"]) {
          std::pair<std::string, uint16_t> channel;
          channel.first = getConfigDataAsString(ch, {"name"});
          if (ch["verbosity"].isNull()) {
            channel.second = logging.verbosity;
          } else {
            channel.second = logger::stringToVerbosity(getConfigDataAsString(ch, {"verbosity"}));
          }
          logging.channels[channel.first] = channel.second;
        }
        for (auto &o : item["outputs"]) {
          logger::Config::OutputConfig output;
          output.type = getConfigDataAsString(o, {"type"});
          output.format = getConfigDataAsString(o, {"format"});
          if (output.type == "file") {
            output.target = log_path;
            output.file_name = (log_path / getConfigDataAsString(o, {"file_name"})).string();
            output.format = getConfigDataAsString(o, {"format"});
            output.max_size = getConfigDataAsUInt(o, {"max_size"});
            output.rotation_size = getConfigDataAsUInt(o, {"rotation_size"});
            output.time_based_rotation = getConfigDataAsString(o, {"time_based_rotation"});
          }
          logging.outputs.push_back(output);
        }
        res.push_back(logging);
      }
    }
  }
  return res;
}

void FullNodeConfig::overwriteConfigFromJson(const Json::Value &root) {
  data_path = getConfigDataAsString(root, {"data_path"});
  db_path = data_path / "db";

  final_chain_cache_in_blocks =
      getConfigDataAsUInt(root, {"final_chain_cache_in_blocks"}, true, final_chain_cache_in_blocks);

  // config values that limits transactions and blocks memory pools
  transactions_pool_size = getConfigDataAsUInt(root, {"transactions_pool_size"}, true, kDefaultTransactionPoolSize);

  blocks_gas_pricer = getConfigDataAsBoolean(root, {"blocks_gas_pricer"}, true, blocks_gas_pricer);

  dec_json(root["network"], network);

  dec_json(root["db_config"], db_config);

  log_configs = loadLoggingConfigs(root["logging"]);

  report_malicious_behaviour =
      getConfigDataAsUInt(root, {"report_malicious_behaviour"}, true, report_malicious_behaviour);
}

FullNodeConfig::FullNodeConfig(const Json::Value &string_or_object, const std::vector<Json::Value> &wallets_jsons,
                               const Json::Value &genesis_json, const std::string &config_file_path) {
  Json::Value parsed_from_file = getJsonFromFileOrString(string_or_object);
  if (string_or_object.isString()) {
    json_file_name = string_or_object.asString();
  } else {
    json_file_name = config_file_path;
  }
  assert(!json_file_name.empty());

  auto const &root = string_or_object.isString() ? parsed_from_file : string_or_object;
  overwriteConfigFromJson(root);

  if (const auto &v = genesis_json; v.isObject()) {
    dec_json(v, genesis);
  } else {
    genesis = GenesisConfig();
  }

  propose_dag_gas_limit = getConfigDataAsUInt(root, {"propose_dag_gas_limit"}, true, propose_dag_gas_limit);
  propose_pbft_gas_limit = getConfigDataAsUInt(root, {"propose_pbft_gas_limit"}, true, propose_pbft_gas_limit);

  for (const auto &wallet_json : wallets_jsons) {
    dev::Secret node_secret;
    vrf_wrapper::vrf_sk_t vrf_secret;

    try {
      node_secret = dev::Secret(wallet_json["node_secret"].asString(), dev::Secret::ConstructFromStringType::FromHex);
      if (!wallet_json["node_public"].isNull()) {
        auto node_public =
            dev::Public(wallet_json["node_public"].asString(), dev::Public::ConstructFromStringType::FromHex);
        if (node_public != dev::KeyPair(node_secret).pub()) {
          throw ConfigException(std::string("Node secret key and public key in wallet do not match"));
        }
      }
      if (!wallet_json["node_address"].isNull()) {
        auto node_address =
            dev::Address(wallet_json["node_address"].asString(), dev::Address::ConstructFromStringType::FromHex);
        if (node_address != dev::KeyPair(node_secret).address()) {
          throw ConfigException(std::string("Node secret key and address in wallet do not match"));
        }
      }
    } catch (const dev::Exception &e) {
      throw ConfigException(std::string("Could not parse node_secret: ") + e.what());
    }

    try {
      vrf_secret = vrf_wrapper::vrf_sk_t(wallet_json["vrf_secret"].asString());
    } catch (const dev::Exception &e) {
      throw ConfigException(std::string("Could not parse vrf_secret: ") + e.what());
    }

    try {
      if (!wallet_json["vrf_public"].isNull()) {
        auto vrf_public = vrf_wrapper::vrf_pk_t(wallet_json["vrf_public"].asString());
        if (vrf_public != ebla::vrf_wrapper::getVrfPublicKey(vrf_secret)) {
          throw ConfigException(std::string("Vrf secret key and public key in wallet do not match"));
        }
      }
    } catch (const dev::Exception &e) {
      throw ConfigException(std::string("Could not parse vrf_public: ") + e.what());
    }

    auto wallet_config = WalletConfig(std::move(node_secret), vrf_secret);
    // Check for duplicate wallets
    if (std::any_of(wallets.cbegin(), wallets.cend(), [&wallet_config](const WalletConfig &wallet) {
          return wallet_config.node_secret == wallet.node_secret || wallet_config.vrf_secret == wallet.vrf_secret;
        })) {
      throw ConfigException(std::string("Duplicate wallets"));
    }

    wallets.push_back(std::move(wallet_config));
  }

  // TODO configurable
  opts_final_chain.expected_max_trx_per_block = 1000;
  opts_final_chain.max_trie_full_node_levels_to_cache = 4;
}

void FullNodeConfig::InitLogging(const addr_t &node_address) {
  for (auto &logging : log_configs) {
    logging.InitLogging(node_address);
  }
}

const WalletConfig &FullNodeConfig::getFirstWallet() const { return wallets.front(); }

void FullNodeConfig::validate() const {
  genesis.validate();
  network.validate(genesis.state.dpos.delegation_delay);

  if (transactions_pool_size < kMinTransactionPoolSize) {
    throw ConfigException("transactions_pool_size cannot be smaller than " + std::to_string(kMinTransactionPoolSize));
  }

  // TODO: add validation of other config values
}

std::ostream &operator<<(std::ostream &strm, const NodeConfig &conf) {
  strm << "  [Node Config] " << std::endl;
  strm << "    node_id: " << conf.id << std::endl;
  strm << "    node_ip: " << conf.ip << std::endl;
  strm << "    node_udp_port: " << conf.port << std::endl;
  return strm;
}

std::ostream &operator<<(std::ostream &strm, const DdosProtectionConfig &conf) {
  strm << "  [Ddos protection] " << std::endl;
  strm << "    vote_accepting_periods: " << conf.vote_accepting_periods << std::endl;
  strm << "    vote_accepting_rounds: " << conf.vote_accepting_rounds << std::endl;
  strm << "    vote_accepting_steps: " << conf.vote_accepting_steps << std::endl;
  strm << "    log_packets_stats: " << conf.log_packets_stats << std::endl;
  strm << "    packets_stats_time_period_ms: " << conf.packets_stats_time_period_ms.count() << std::endl;
  strm << "    peer_max_packets_processing_time_us: " << conf.peer_max_packets_processing_time_us.count() << std::endl;
  strm << "    peer_max_packets_queue_size_limit: " << conf.peer_max_packets_queue_size_limit << std::endl;
  strm << "    max_packets_queue_size: " << conf.max_packets_queue_size << std::endl;
  return strm;
}

std::ostream &operator<<(std::ostream &strm, const NetworkConfig &conf) {
  strm << "[Network Config] " << std::endl;
  strm << "  json_file_name: " << conf.json_file_name << std::endl;
  strm << "  listen_ip: " << conf.listen_ip << std::endl;
  strm << "  public_ip: " << conf.public_ip << std::endl;
  strm << "  listen_port: " << conf.listen_port << std::endl;
  strm << "  transaction_interval_ms: " << conf.transaction_interval_ms << std::endl;
  strm << "  ideal_peer_count: " << conf.ideal_peer_count << std::endl;
  strm << "  max_peer_count: " << conf.max_peer_count << std::endl;
  strm << "  sync_level_size: " << conf.sync_level_size << std::endl;
  strm << "  num_threads: " << conf.num_threads << std::endl;
  strm << "  packets_processing_threads: " << conf.packets_processing_threads << std::endl;
  strm << "  deep_syncing_threshold: " << conf.deep_syncing_threshold << std::endl;
  strm << conf.ddos_protection << std::endl;

  strm << "  --> boot nodes  ... " << std::endl;
  for (const auto &c : conf.boot_nodes) {
    strm << c << std::endl;
  }
  return strm;
}

std::ostream &operator<<(std::ostream &strm, const FullNodeConfig &conf) {
  strm << std::ifstream(conf.json_file_name).rdbuf() << std::endl;
  return strm;
}
}  // namespace ebla
