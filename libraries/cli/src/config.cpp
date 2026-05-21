#include "cli/config.hpp"

#include <libdevcore/CommonJS.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>

#include "cli/config_updater.hpp"
#include "cli/tools.hpp"
#include "common/jsoncpp.hpp"
#include "config/version.hpp"

namespace ebla::cli {

// EBLA DB_ROADMAP_v01 §Docker-step — Helpers for parsing the new DB tunable
// CLI flags. All have internal linkage to this translation unit.
//
// TODO(post-roadmap): hoist these into libraries/common/config_utils + a shared
// validation helper so file 3 (config/src/config.cpp) and this file can share
// one implementation. Deliberately duplicated for now to keep this PR minimal.

// Parse a byte-size string. Accepts:
//   - Raw integer: "2147483648"
//   - Integer + suffix (case-insensitive): "2GB", "512MiB", "1KB", "10TiB"
//   Binary suffixes (KiB, MiB, GiB, TiB) multiply by 1024^n.
//   Decimal suffixes (KB, MB, GB, TB) multiply by 1000^n.
//   Bare suffix B == 1.
// Throws bpo::invalid_option_value on negative, malformed, or overflowing input.
static uint64_t parseByteSize(const std::string& flag_name, const std::string& s) {
  if (s.empty()) {
    throw bpo::invalid_option_value(flag_name + ": empty value");
  }
  // Locate the numeric prefix.
  size_t pos = 0;
  while (pos < s.size() && (std::isdigit(static_cast<unsigned char>(s[pos])) || s[pos] == '+')) {
    ++pos;
  }
  if (pos == 0) {
    throw bpo::invalid_option_value(flag_name + ": '" + s + "' (must start with a non-negative integer)");
  }
  const std::string num_part = s.substr(0, pos);
  std::string suffix = s.substr(pos);
  // Lowercase the suffix in place.
  std::transform(suffix.begin(), suffix.end(), suffix.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

  uint64_t base;
  try {
    base = std::stoull(num_part);
  } catch (const std::exception&) {
    throw bpo::invalid_option_value(flag_name + ": '" + s + "' (numeric overflow or malformed)");
  }

  uint64_t multiplier = 1;
  if (suffix.empty() || suffix == "b") {
    multiplier = 1;
  } else if (suffix == "kb") {
    multiplier = 1000ULL;
  } else if (suffix == "kib") {
    multiplier = 1024ULL;
  } else if (suffix == "mb") {
    multiplier = 1000ULL * 1000;
  } else if (suffix == "mib") {
    multiplier = 1024ULL * 1024;
  } else if (suffix == "gb") {
    multiplier = 1000ULL * 1000 * 1000;
  } else if (suffix == "gib") {
    multiplier = 1024ULL * 1024 * 1024;
  } else if (suffix == "tb") {
    multiplier = 1000ULL * 1000 * 1000 * 1000;
  } else if (suffix == "tib") {
    multiplier = 1024ULL * 1024 * 1024 * 1024;
  } else {
    throw bpo::invalid_option_value(flag_name + ": '" + s + "' (unknown suffix '" + suffix +
                                    "'; expected B/KB/KiB/MB/MiB/GB/GiB/TB/TiB)");
  }

  // Overflow-safe multiply.
  if (multiplier != 0 && base > (std::numeric_limits<uint64_t>::max() / multiplier)) {
    throw bpo::invalid_option_value(flag_name + ": '" + s + "' (overflows uint64_t)");
  }
  return base * multiplier;
}

// Parse a boolean string. Strictly accepts "true"/"false" (case-insensitive)
// to reduce support tickets like "I set EBLA_DB_TIERING_ENABLED=1 but tiering
// didn't activate" (see DB_ROADMAP_v01 Mandatory Security Check §4 of the
// Docker-step).
static bool parseBool(const std::string& flag_name, const std::string& s) {
  std::string lower(s);
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (lower == "true") return true;
  if (lower == "false") return false;
  throw bpo::invalid_option_value(flag_name + ": '" + s + "' (must be 'true' or 'false')");
}

// Same allow-list path-prefix check as libraries/config/src/config.cpp.
// See TODO note above re: hoisting.
static void validateArchivePath(const std::filesystem::path& p) {
  const auto str = p.string();
  if (str.empty()) {
    throw bpo::invalid_option_value(std::string(DB_ARCHIVE_PATH) + ": empty");
  }
  if (str.size() > 1024) {
    throw bpo::invalid_option_value(std::string(DB_ARCHIVE_PATH) + ": too long (max 1024 chars)");
  }
  if (!p.is_absolute()) {
    throw bpo::invalid_option_value(std::string(DB_ARCHIVE_PATH) + ": must be absolute (got: " + str + ")");
  }
  static constexpr std::array<const char*, 4> kAllowedPrefixes = {"/mnt/", "/srv/", "/opt/", "/var/lib/"};
  for (const auto* prefix : kAllowedPrefixes) {
    if (str.rfind(prefix, 0) == 0) {
      return;
    }
  }
  throw bpo::invalid_option_value(std::string(DB_ARCHIVE_PATH) +
                                  ": must be under /mnt, /srv, /opt, or /var/lib (got: " + str + ")");
}

Config::Config() : plugins_options_("PLUGINS") {}

void Config::addCliOptions(const bpo::options_description& options) { plugins_options_.add(options); }

void Config::parseCommandLine(int argc, const char* argv[], const std::string& available_plugins) {
  bpo::options_description allowed_options("");
  auto main_options = makeMainOptions();
  auto node_command_options = makeNodeOptions(available_plugins);
  allowed_options.add(main_options);
  allowed_options.add(node_command_options);
  allowed_options.add(plugins_options_);

  auto parsed_line = bpo::parse_command_line(argc, argv, allowed_options);

  bpo::store(parsed_line, cli_options_);
  bpo::notify(cli_options_);
  if (cli_options_.count(HELP)) {
    std::cout << "NAME:\n  "
                 "eblad - Ebla blockchain full node implementation\n"
                 "VERSION:\n  "
              << EBLA_VERSION << "\nUSAGE:\n  eblad [options]\n";
    std::cout << allowed_options << std::endl;
    // std::cout << node_command_options << std::endl;
    // If help message requested, ignore any additional commands
    return;
  }
  if (cli_options_.count(VERSION)) {
    std::cout << kVersionJson << std::endl;
    return;
  }
  if (cli_options_.count(PLUGINS)) {
    plugins_ = cli_options_[PLUGINS].as<std::vector<std::string>>();
  }
  if (cli_options_[LIGHT].as<bool>()) {
    plugins_.emplace_back("light");
  }

  std::vector<std::string> command;
  if (cli_options_.count(COMMAND)) {
    command = cli_options_[COMMAND].as<std::vector<std::string>>();
  }
  if (command.empty()) {
    command.push_back(NODE_COMMAND);
  }

  if (command[0] == NODE_COMMAND || command[0] == CONFIG_COMMAND) {
    // Create dir if missing
    auto config_dir = dirNameFromFile(config);
    if (!config_dir.empty() && !fs::exists(config_dir)) {
      fs::create_directories(config_dir);
    }

    auto genesis_dir = dirNameFromFile(genesis);
    if (!genesis_dir.empty() && !fs::exists(genesis_dir)) {
      fs::create_directories(genesis_dir);
    }

    for (const auto& wallet : wallets) {
      auto wallet_dir = dirNameFromFile(wallet);
      if (!wallet_dir.empty() && !fs::exists(wallet_dir)) {
        fs::create_directories(wallet_dir);
      }
    }

    // Update chain_id
    int chain_id = static_cast<int>(DEFAULT_CHAIN_ID);
    if (cli_options_.count(CHAIN_ID)) {
      if (cli_options_.count(CHAIN)) {
        std::cout << "You can not specify both " << CHAIN_ID << " and " << CHAIN << std::endl;
        return;
      }
      chain_id = cli_options_[CHAIN_ID].as<int>();
    }
    if (cli_options_.count(CHAIN)) {
      chain_id = tools::getChainIdFromString(cli_options_[CHAIN].as<std::string>());
    }

    // If any of the config files are missing they are generated with default values
    if (!fs::exists(config)) {
      std::cout << "Configuration file does not exist at: " << config << ". New config file will be generated"
                << std::endl;
      util::writeJsonToFile(config, tools::getConfig((Config::ChainIdType)chain_id));
    }

    if (!fs::exists(genesis)) {
      std::cout << "Genesis file does not exist at: " << genesis << ". New one file will be generated" << std::endl;
      util::writeJsonToFile(genesis, tools::getGenesis((Config::ChainIdType)chain_id));
    }

    for (const auto& wallet : wallets) {
      if (!fs::exists(wallet)) {
        std::cout << "Wallet file does not exist at: " << wallet << ". New wallet file will be generated" << std::endl;
        tools::generateWallet(wallet);
      }
    }

    Json::Value config_json = util::readJsonFromFile(config);
    Json::Value genesis_json = util::readJsonFromFile(genesis);
    std::vector<Json::Value> wallets_jsons;
    for (const auto& wallet : wallets) {
      wallets_jsons.push_back(util::readJsonFromFile(wallet));
    }

    auto write_config_and_wallet_files = [&]() {
      try {
        util::writeJsonToFile(config, config_json);
      } catch (const std::exception& e) {
        std::cerr << "Error writing to config file at path " << config << ": " << e.what() << std::endl;
      }
      util::writeJsonToFile(genesis, genesis_json);

      assert(wallets_jsons.size() <= wallets.size());
      size_t idx = 0;
      for (const auto& wallet_json : wallets_jsons) {
        try {
          util::writeJsonToFile(wallets[idx++], wallet_json);
        } catch (const std::exception& e) {
          std::cerr << "Error writing wallet file " << wallets[idx - 1] << ": " << e.what() << std::endl;
        }
      }
    };

    // Check that it is not empty, to not create chain config with just overwritten files
    if (!genesis_json.isNull()) {
      auto default_genesis_json = tools::getGenesis((Config::ChainIdType)genesis_json["chain_id"].asUInt64());
      // override protocol data with one from default json
      genesis_json["protocol"] = default_genesis_json["protocol"];
      write_config_and_wallet_files();
    }

    // Override config values with values from CLI
    if (cli_options_.count(DATA_DIR)) {
      data_dir = cli_options_[DATA_DIR].as<std::string>();
    }
    std::vector<std::string> boot_nodes;
    if (cli_options_.count(BOOT_NODES)) {
      boot_nodes = cli_options_[BOOT_NODES].as<std::vector<std::string>>();
    }
    std::vector<std::string> log_channels;
    if (cli_options_.count(LOG_CHANNELS)) {
      log_channels = cli_options_[LOG_CHANNELS].as<std::vector<std::string>>();
    }
    std::vector<std::string> log_configurations;
    if (cli_options_.count(LOG_CONFIGURATIONS)) {
      log_configurations = cli_options_[LOG_CONFIGURATIONS].as<std::vector<std::string>>();
    }
    std::vector<std::string> boot_nodes_append;
    if (cli_options_.count(BOOT_NODES_APPEND)) {
      boot_nodes_append = cli_options_[BOOT_NODES_APPEND].as<std::vector<std::string>>();
    }
    std::vector<std::string> log_channels_append;
    if (cli_options_.count(LOG_CHANNELS_APPEND)) {
      log_channels_append = cli_options_[LOG_CHANNELS_APPEND].as<std::vector<std::string>>();
    }
    std::string node_secret;
    if (cli_options_.count(NODE_SECRET)) {
      node_secret = cli_options_[NODE_SECRET].as<std::string>();
    }
    std::string vrf_secret;
    if (cli_options_.count(VRF_SECRET)) {
      vrf_secret = cli_options_[VRF_SECRET].as<std::string>();
    }
    // Override only first wallet
    auto& first_wallet_json = wallets_jsons.front();
    first_wallet_json = tools::overrideWallet(first_wallet_json, node_secret, vrf_secret);

    // Create data directory
    if (!data_dir.empty() && !fs::exists(data_dir)) {
      fs::create_directories(data_dir);
    }

    // Check that it is not empty, to not create chain config with just overwritten files
    if (!genesis_json.isNull()) {
      auto default_genesis_json = tools::getGenesis((Config::ChainIdType)genesis_json["chain_id"].asUInt64());
      // override protocol data with one from default json
      genesis_json["protocol"] = default_genesis_json["protocol"];
      util::writeJsonToFile(genesis, genesis_json);
    }

    config_json = tools::overrideConfig(config_json, data_dir, boot_nodes, log_channels, log_configurations,
                                        boot_nodes_append, log_channels_append);
    config_json["is_light_node"] = cli_options_[LIGHT].as<bool>();
    {
      ConfigUpdater updater{chain_id};
      updater.UpdateConfig(config_json);
      util::writeJsonToFile(config, config_json);
    }

    // Load config
    node_config_ = FullNodeConfig(config_json, wallets_jsons, genesis_json, config);

    // Save changes permanently if overwrite_config option is set
    // or if running config command
    // This can overwrite secret keys in wallet
    if (overwrite_config || command[0] == CONFIG_COMMAND) {
      genesis_json = enc_json(node_config_.genesis);
      write_config_and_wallet_files();
    }

    // Validate config values
    node_config_.validate();

    if (cli_options_[DESTROY_DB].as<bool>()) {
      fs::remove_all(node_config_.db_path);
    }
    if (cli_options_[REBUILD_NETWORK].as<bool>()) {
      fs::remove_all(node_config_.net_file_path());
    }
    if (cli_options_.count(PUBLIC_IP) && !cli_options_[PUBLIC_IP].as<std::string>().empty()) {
      node_config_.network.public_ip = cli_options_[PUBLIC_IP].as<std::string>();
    }
    if (cli_options_.count(PORT) && cli_options_[PORT].as<uint16_t>() != 0) {
      node_config_.network.listen_port = cli_options_[PORT].as<uint16_t>();
    }
    node_config_.db_config.db_revert_to_period = cli_options_[REVERT_TO_PERIOD].as<uint64_t>();
    node_config_.db_config.rebuild_db = cli_options_[REBUILD_DB].as<bool>();
    node_config_.db_config.rebuild_db_period = cli_options_[REBUILD_DB_PERIOD].as<uint64_t>();
    node_config_.db_config.migrate_only = cli_options_[MIGRATE_ONLY].as<bool>();
    node_config_.db_config.migrate_receipts_by_period = cli_options_[MIGRATE_RECEIPTS_BY_PERIOD].as<bool>();

    // EBLA DB_ROADMAP_v01 §Docker-step — Apply DB tunable overrides AFTER JSON load.
    // Each flag uses cli_options_.count(NAME) to detect "was this flag provided
    // on the command line at all?" (boost::program_options sets count > 0 only
    // for explicitly provided values, since these flags have no default_value).
    // Re-runs the same validation invariants dec_json(DBConfig&) enforces, so a
    // CLI override cannot put db_config into a state JSON parsing would reject.
    if (cli_options_.count(DB_BLOCK_CACHE_SIZE)) {
      const auto v = parseByteSize(DB_BLOCK_CACHE_SIZE, cli_options_[DB_BLOCK_CACHE_SIZE].as<std::string>());
      if (v < (16ULL << 20) || v > (1ULL << 40)) {
        throw bpo::invalid_option_value(
            std::string(DB_BLOCK_CACHE_SIZE) + ": out of range (16 MiB .. 1 TiB)");
      }
      node_config_.db_config.db_block_cache_size_bytes = v;
    }
    if (cli_options_.count(DB_WRITE_BUFFER_SIZE)) {
      const auto v = parseByteSize(DB_WRITE_BUFFER_SIZE, cli_options_[DB_WRITE_BUFFER_SIZE].as<std::string>());
      if (v < (256ULL << 20) || v > (32ULL << 30)) {
        throw bpo::invalid_option_value(
            std::string(DB_WRITE_BUFFER_SIZE) + ": out of range (256 MiB .. 32 GiB)");
      }
      node_config_.db_config.db_write_buffer_size_bytes = v;
    }
    if (cli_options_.count(DB_MAX_OPEN_FILES)) {
      node_config_.db_config.db_max_open_files = cli_options_[DB_MAX_OPEN_FILES].as<uint32_t>();
    }
    if (cli_options_.count(DB_TIERING_ENABLED)) {
      node_config_.db_config.db_tiering_enabled =
          parseBool(DB_TIERING_ENABLED, cli_options_[DB_TIERING_ENABLED].as<std::string>());
    }
    if (cli_options_.count(DB_ARCHIVE_PATH)) {
      node_config_.db_config.db_archive_path =
          std::filesystem::path(cli_options_[DB_ARCHIVE_PATH].as<std::string>());
    }
    if (cli_options_.count(DB_HOT_SIZE_LIMIT)) {
      node_config_.db_config.db_hot_size_limit_bytes =
          parseByteSize(DB_HOT_SIZE_LIMIT, cli_options_[DB_HOT_SIZE_LIMIT].as<std::string>());
    }
    if (cli_options_.count(DB_COLD_COMPRESSION_LEVEL)) {
      const auto lvl = cli_options_[DB_COLD_COMPRESSION_LEVEL].as<uint32_t>();
      if (lvl < 1 || lvl > 22) {
        throw bpo::invalid_option_value(
            std::string(DB_COLD_COMPRESSION_LEVEL) + ": out of range (1..22)");
      }
      node_config_.db_config.db_cold_compression_level = static_cast<uint8_t>(lvl);
    }
    // Cross-field re-validation after all overrides applied (mirrors dec_json).
    if (node_config_.db_config.db_tiering_enabled) {
      validateArchivePath(node_config_.db_config.db_archive_path);
      if (node_config_.db_config.db_hot_size_limit_bytes < (10ULL << 30)) {
        throw bpo::invalid_option_value(
            std::string(DB_HOT_SIZE_LIMIT) + ": must be >= 10 GiB when tiering is enabled");
      }
    }

    if (command[0] == NODE_COMMAND) node_configured_ = true;
  } else if (command[0] == ACCOUNT_COMMAND) {
    if (command.size() == 1)
      tools::generateAccount();
    else
      tools::generateAccountFromKey(command[1]);
  } else if (command[0] == VRF_COMMAND) {
    if (command.size() == 1)
      tools::generateVrf();
    else
      tools::generateVrfFromKey(command[1]);
  } else {
    throw bpo::invalid_option_value(command[0]);
  }
}

bool Config::nodeConfigured() const { return node_configured_; }

FullNodeConfig Config::getNodeConfiguration() const { return node_config_; }

std::string Config::dirNameFromFile(const std::string& file) {
  size_t pos = file.find_last_of("\\/");
  return (std::string::npos == pos) ? "" : file.substr(0, pos);
}

bpo::options_description Config::makeMainOptions() {
  bpo::options_description main_options("OPTIONS");

  // Define all the command line options and descriptions
  main_options.add_options()(HELP, "Print this help message and exit");
  main_options.add_options()(VERSION, "Print version of eblad");

  main_options.add_options()(COMMAND, bpo::value<std::vector<std::string>>()->multitoken(),
                             "Command arg:"
                             "\nnode                  Runs the actual node (default)"
                             "\nconfig       Only generate/overwrite config file with provided node command "
                             "option without starting the node"
                             "\naccount key           Generate new account or restore from a key (key is optional)"
                             "\nvrf key               Generate new VRF or restore from a key (key is optional)");
  return main_options;
}

bpo::options_description Config::makeNodeOptions(const std::string& available_plugins) {
  bpo::options_description node_command_options("NODE COMMAND OPTIONS");
  // Set config file and data directory to default values
  config = tools::getEblaDefaultConfigFile();
  wallets = {tools::getEblaDefaultWalletFile()};
  genesis = tools::getEblaDefaultGenesisFile();

  auto plugins_desc = "List of plugins to activate separated by space: " + available_plugins +
                      " (default: " + std::accumulate(plugins_.begin(), plugins_.end(), std::string()) + ")";
  node_command_options.add_options()(PLUGINS, bpo::value<std::vector<std::string>>()->multitoken()->composing(),
                                     plugins_desc.c_str());
  node_command_options.add_options()(WALLET, bpo::value<std::vector<std::string>>(&wallets)->multitoken(),
                                     "JSON wallet file(s) (default: \"~/.ebla/wallet.json\")");
  node_command_options.add_options()(CONFIG, bpo::value<std::string>(&config),
                                     "JSON configuration file (default: \"~/.ebla/config.json\")");
  node_command_options.add_options()(GENESIS, bpo::value<std::string>(&genesis),
                                     "JSON genesis file (default: \"~/.ebla/genesis.json\")");
  node_command_options.add_options()(DATA_DIR, bpo::value<std::string>(&data_dir),
                                     "Data directory for the databases, logs ... (default: \"~/.ebla/data\")");
  node_command_options.add_options()(LIGHT, bpo::bool_switch()->default_value(false),
                                     "Enable light node functionality");
  node_command_options.add_options()(
      CHAIN_ID, bpo::value<int>(),
      "Chain identifier (integer, 60186=Mainnet, 60187=Testnet, 60188=Devnet) (default: 60186) "
      "Only used when creating new config file");
  node_command_options.add_options()(CHAIN, bpo::value<std::string>(),
                                     "Chain identifier (string, mainnet, testnet, devnet) (default: mainnet) "
                                     "Only used when creating new config file");

  node_command_options.add_options()(BOOT_NODES, bpo::value<std::vector<std::string>>()->multitoken(),
                                     "Boot nodes to connect to: [ip_address:port_number/node_id, ....]");
  node_command_options.add_options()(
      BOOT_NODES_APPEND, bpo::value<std::vector<std::string>>()->multitoken(),
      "Boot nodes to connect to in addition to boot nodes defined in config: [ip_address:port_number/node_id, ....]");
  node_command_options.add_options()(PUBLIC_IP, bpo::value<std::string>(),
                                     "Force advertised public IP to the given IP (default: auto)");
  node_command_options.add_options()(PORT, bpo::value<uint16_t>(), "Listen on the given port for incoming connections");
  node_command_options.add_options()(LOG_CHANNELS, bpo::value<std::vector<std::string>>()->multitoken(),
                                     "Log channels to log: [channel:level, ....]");
  node_command_options.add_options()(
      LOG_CHANNELS_APPEND, bpo::value<std::vector<std::string>>()->multitoken(),
      "Log channels to log in addition to log channels defined in config: [channel:level, ....]");
  node_command_options.add_options()(LOG_CONFIGURATIONS, bpo::value<std::vector<std::string>>()->multitoken(),
                                     "Log configurations to use: [configuration_name, ....]");
  node_command_options.add_options()(NODE_SECRET, bpo::value<std::string>(), "Node secret key to use");

  node_command_options.add_options()(VRF_SECRET, bpo::value<std::string>(), "Vrf secret key to use");

  node_command_options.add_options()(
      OVERWRITE_CONFIG, bpo::bool_switch(&overwrite_config),
      "Overwrite config - "
      "Options data-dir, boot-nodes, log-channels, node-secret and vrf-secret are always used in running a node but "
      "only written to config file if overwrite-config flag is set. \n"
      "WARNING: Overwrite-config set can override/delete current secret keys in the wallet");

  // db related options
  node_command_options.add_options()(DESTROY_DB, bpo::bool_switch()->default_value(false),
                                     "Destroys all the existing data in the database");
  node_command_options.add_options()(REBUILD_DB, bpo::bool_switch()->default_value(false),
                                     "Reads the raw dag/pbft blocks from the db "
                                     "and executes all the blocks from scratch "
                                     "rebuilding all the other "
                                     "database tables - this could take a long "
                                     "time");
  node_command_options.add_options()(REBUILD_DB_PERIOD, bpo::value<uint64_t>()->default_value(0),
                                     "Use with rebuild-db - Rebuild db up "
                                     "to a specified period");
  node_command_options.add_options()(REBUILD_NETWORK, bpo::bool_switch()->default_value(false),
                                     "Delete all saved network/nodes information "
                                     "and rebuild network from boot nodes");
  node_command_options.add_options()(REVERT_TO_PERIOD, bpo::value<uint64_t>()->default_value(0),
                                     "Revert db/state to specified "
                                     "period (specify period)");
  // migration related options
  node_command_options.add_options()(MIGRATE_ONLY, bpo::bool_switch()->default_value(false),
                                     "Only migrate DB, it will NOT run a node");
  node_command_options.add_options()(MIGRATE_RECEIPTS_BY_PERIOD, bpo::bool_switch()->default_value(false),
                                     "Apply migration to store receipts by period, not by hash");

  // EBLA DB_ROADMAP_v01 §Docker-step — DB tunable overrides.
  // Each flag is optional (no default_value()); if absent at startup, the
  // value parsed from the JSON config file is preserved unchanged. The
  // override block below in parseCommandLine() applies these AFTER JSON load.
  node_command_options.add_options()(DB_BLOCK_CACHE_SIZE, bpo::value<std::string>(),
      "RocksDB block cache size. Accepts raw bytes or human suffix (e.g., '2GB', '512MiB'). "
      "Overrides db_block_cache_size_bytes in JSON.");
  node_command_options.add_options()(DB_WRITE_BUFFER_SIZE, bpo::value<std::string>(),
      "RocksDB MemTable budget. Accepts raw bytes or human suffix (e.g., '2GB'). "
      "Overrides db_write_buffer_size_bytes in JSON.");
  node_command_options.add_options()(DB_MAX_OPEN_FILES, bpo::value<uint32_t>(),
      "RocksDB max open SST file descriptors. Overrides db_max_open_files in JSON.");
  node_command_options.add_options()(DB_TIERING_ENABLED, bpo::value<std::string>(),
      "Enable hot/cold tiered storage: 'true' or 'false'. Overrides db_tiering_enabled in JSON.");
  node_command_options.add_options()(DB_ARCHIVE_PATH, bpo::value<std::string>(),
      "Absolute path to cold-tier (HDD) directory. Must be under /mnt, /srv, /opt, or /var/lib. "
      "Overrides db_archive_path in JSON.");
  node_command_options.add_options()(DB_HOT_SIZE_LIMIT, bpo::value<std::string>(),
      "Hot-tier size budget. Accepts raw bytes or human suffix (e.g., '800GB'). "
      "Overrides db_hot_size_limit_bytes in JSON.");
  node_command_options.add_options()(DB_COLD_COMPRESSION_LEVEL, bpo::value<uint32_t>(),
      "ZSTD compression level for cold-tier SSTs, range 1..22. Overrides db_cold_compression_level in JSON.");
  return node_command_options;
}

}  // namespace ebla::cli
