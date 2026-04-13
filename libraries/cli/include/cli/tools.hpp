#pragma once

#include <json/json.h>
#include <libdevcrypto/Common.h>

#include <string>

#include "cli/config.hpp"
#include "common/vrf_wrapper.hpp"

namespace ebla::cli::tools {

constexpr const char* DEFAULT_EBLA_DIR_NAME = ".ebla";
constexpr const char* DEFAULT_EBLA_DATA_DIR_NAME = ".ebla/data";
constexpr const char* DEFAULT_WALLET_FILE_NAME = "wallet.json";
constexpr const char* DEFAULT_CONFIG_FILE_NAME = "config.json";
constexpr const char* DEFAULT_GENESIS_FILE_NAME = "genesis.json";
const std::filesystem::path configs_path = std::filesystem::current_path() / "config_jsons";

// Account generation
void generateAccount();
void generateAccount(const dev::KeyPair& account);
void generateAccountFromKey(const std::string& key);

// VRF generation
void generateVrf();
void generateVrf(const ebla::vrf_wrapper::vrf_sk_t& sk, const ebla::vrf_wrapper::vrf_pk_t& pk);
void generateVrfFromKey(const std::string& key);

// Generate default config and wallet files
int getChainIdFromString(std::string chain_str);
void getConfig(const std::string& config, cli::Config::ChainIdType chain_id);
Json::Value getConfig(Config::ChainIdType chain_id);
Json::Value getGenesis(Config::ChainIdType chain_id);
void generateWallet(const std::string& wallet);

// Override existing config and wallet files
Json::Value overrideConfig(Json::Value& config, std::string& data_dir, const std::vector<std::string>& boot_nodes,
                           const std::vector<std::string>& log_channels,
                           const std::vector<std::string>& log_configurations,
                           const std::vector<std::string>& boot_nodes_append,
                           const std::vector<std::string>& log_channels_append);
Json::Value overrideWallet(Json::Value& wallet, const std::string& node_key, const std::string& vrf_key);

std::string getHomeDir();
std::string getEblaDefaultDir();
std::string getEblaDataDefaultDir();
std::string getEblaDefaultConfigFile();
std::string getEblaDefaultWalletFile();
std::string getEblaDefaultGenesisFile();
Json::Value createWalletJson(const dev::KeyPair& account, const ebla::vrf_wrapper::vrf_sk_t& sk,
                             const ebla::vrf_wrapper::vrf_pk_t& pk);

}  // namespace ebla::cli::tools