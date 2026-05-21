#pragma once

#include "common/vrf_wrapper.hpp"
#include "config/genesis.hpp"
#include "config/network.hpp"
#include "logger/logger_config.hpp"

namespace ebla {

struct DBConfig {
  uint32_t db_snapshot_each_n_pbft_block = 0;
  uint32_t db_max_snapshots = 0;
  uint32_t db_max_open_files = 0;
  PbftPeriod db_revert_to_period = 0;
  bool rebuild_db = false;
  bool migrate_only = false;
  PbftPeriod rebuild_db_period = 0;
  bool migrate_receipts_by_period = false;
  bool db_compression = true;  // Enable LZ4 compression on RocksDB column families
  // EBLA DB_ROADMAP_v01 §2.2 / §5 — RAM-discipline tunables
  // RocksDB block cache (shared across all CFs when Phase 5 wiring lands).
  // Default sized for the 8 GiB validator profile; 16 GiB validators raise to 2 GiB,
  // 32 GiB premium/RPC nodes raise to 8 GiB. See operator-docs Phase 9.
  uint64_t db_block_cache_size_bytes = 512ULL << 20;  // 512 MiB
  // RocksDB MemTable budget (DB-wide cap, NOT per-CF). Below 256 MiB risks
  // mid-PBFT-round flushes; above 8 GiB on a 16 GiB box risks OOM.
  uint64_t db_write_buffer_size_bytes = 2ULL << 30;  // 2 GiB

  // EBLA DB_ROADMAP_v01 §2.2 / §6–§7 — Tiered storage (cf_paths)
  // Off by default per Phase 11.2. Enabling without setting db_archive_path
  // is rejected at config-parse time.
  bool db_tiering_enabled = false;
  // Absolute path to the HDD/cold-tier mount. Validated against an allow-list
  // of mount prefixes (/mnt, /srv, /opt, /var/lib) in dec_json.
  fs::path db_archive_path;
  // Hot-tier (SSD) size budget in bytes. RocksDB places SSTs on the hot path
  // until this limit is reached, then spills to the cold path. Must be >= 10 GiB
  // when tiering is enabled.
  uint64_t db_hot_size_limit_bytes = 800ULL * (1ULL << 30);  // ~859 GB
  // ZSTD compression level for cold-tier (bottommost) SSTs. Range 1..22.
  // Stored as uint8_t to forbid ZSTD "fast" negative levels (intentional).
  uint8_t db_cold_compression_level = 9;
};
void dec_json(Json::Value const &json, DBConfig &db_config);

struct WalletConfig {
  WalletConfig(dev::Secret &&node_secret, const vrf_wrapper::vrf_sk_t &vrf_secret)
      : node_secret(std::move(node_secret)),
        node_pk(dev::toPublic(node_secret)),
        node_addr(toAddress(node_secret)),
        vrf_secret(vrf_secret),
        vrf_pk(vrf_wrapper::getVrfPublicKey(vrf_secret)) {}
  WalletConfig(const WalletConfig &) = default;
  WalletConfig(WalletConfig &&) = default;

  const dev::Secret node_secret;
  const dev::Public node_pk;
  const addr_t node_addr;

  const vrf_wrapper::vrf_sk_t vrf_secret;
  const vrf_wrapper::vrf_pk_t vrf_pk;
};

struct FullNodeConfig {
  static constexpr uint64_t kDefaultLightNodeHistoryDays = 1;

  FullNodeConfig() = default;
  // The reason of using Json::Value as a union is that in the tests
  // there are attempts to pass char const* to this constructor, which
  // is ambiguous (char const* may promote to Json::Value)
  // if you have std::string and Json::Value constructor. It was easier
  // to just treat Json::Value as a std::string or Json::Value depending on
  // the contents
  explicit FullNodeConfig(const Json::Value &file_name_str_or_json_object,
                          const std::vector<Json::Value> &wallets_jsons, const Json::Value &genesis = Json::Value::null,
                          const std::string &config_file_path = "");

  void overwriteConfigFromJson(const Json::Value &config_json);
  std::vector<logger::Config> loadLoggingConfigs(const Json::Value &logging);
  void InitLogging(const addr_t &node_address);

  /**
   * @return first (main) wallet from the list of wallets
   */
  const WalletConfig &getFirstWallet() const;

  std::string json_file_name;
  std::filesystem::file_time_type last_json_update_time;
  // Vector of wallets used by node
  std::vector<WalletConfig> wallets;
  fs::path data_path;
  fs::path db_path;
  fs::path log_path;
  NetworkConfig network;
  DBConfig db_config;
  GenesisConfig genesis;
  state_api::Opts opts_final_chain;
  std::vector<logger::Config> log_configs;
  bool is_light_node = false;                            // Is light node
  uint64_t light_node_history = 0;                       // Number of periods to keep in history for a light node
  uint32_t dag_expiry_limit = kDagExpiryLevelLimit;      // For unit tests only
  uint32_t max_levels_per_period = kMaxLevelsPerPeriod;  // For unit tests only
  uint32_t final_chain_cache_in_blocks = 5;
  uint64_t propose_dag_gas_limit = 0x1E0A6E0;
  uint64_t propose_pbft_gas_limit = 0x12C684C0;

  // config values that limits transactions pool
  uint32_t transactions_pool_size = kDefaultTransactionPoolSize;

  // Use blocks legacy gas pricer, if false gas pricer is based on transaction pool
  bool blocks_gas_pricer = false;

  // Report malicious behaviour like double voting, etc... to slashing/jailing contract
  bool report_malicious_behaviour = false;

  auto net_file_path() const { return data_path / "net"; }

  /**
   * @brief Validates config values, throws configexception if validation fails
   * @return
   */
  void validate() const;
};

std::ostream &operator<<(std::ostream &strm, NodeConfig const &conf);
std::ostream &operator<<(std::ostream &strm, NetworkConfig const &conf);
std::ostream &operator<<(std::ostream &strm, FullNodeConfig const &conf);

}  // namespace ebla
