#include "config/pbft_config.hpp"

#include <libdevcore/CommonJS.h>
#include <libdevcore/RLP.h>

#include "common/config_exception.hpp"

namespace ebla {

Json::Value enc_json(PbftConfig const& obj) {
  Json::Value ret(Json::objectValue);
  ret["lambda_ms"] = dev::toJS(obj.lambda_ms);
  ret["committee_size"] = dev::toJS(obj.committee_size);
  ret["number_of_proposers"] = dev::toJS(obj.number_of_proposers);
  ret["dag_blocks_size"] = dev::toJS(obj.dag_blocks_size);
  ret["ghost_path_move_back"] = dev::toJS(obj.ghost_path_move_back);
  ret["gas_limit"] = dev::toJS(obj.gas_limit);
  // === EBLA ADDITION (Layer 2) ===
  ret["max_stuck_rounds"] = dev::toJS(obj.max_stuck_rounds);
  // === END EBLA ADDITION ===
  return ret;
}

void dec_json(Json::Value const& json, PbftConfig& obj) {
  obj.lambda_ms = dev::jsToInt(json["lambda_ms"].asString());
  obj.committee_size = dev::jsToInt(json["committee_size"].asString());
  obj.number_of_proposers = dev::jsToInt(json["number_of_proposers"].asString());
  obj.dag_blocks_size = dev::jsToInt(json["dag_blocks_size"].asString());
  obj.ghost_path_move_back = dev::jsToInt(json["ghost_path_move_back"].asString());
  obj.gas_limit = dev::getUInt(json["gas_limit"]);
  // === EBLA ADDITION (Layer 2) ===
  // Backward-compatible: missing key falls back to the in-class default (50).
  // This protects test fixtures and any older config JSONs that predate L2.
  if (json.isMember("max_stuck_rounds")) {
    const auto parsed = dev::jsToInt(json["max_stuck_rounds"].asString());
    if (parsed < 10) {
      throw ConfigException(
          "pbft.max_stuck_rounds must be >= 10 "
          "(values below this can prevent legitimate consensus from converging "
          "and produce a chain that finalises only NULL-anchor periods)");
    }
    obj.max_stuck_rounds = parsed;
  }
  // === END EBLA ADDITION ===
}

bytes PbftConfig::rlp() const {
  dev::RLPStream s;
  s.appendList(6);

  s << lambda_ms;
  s << committee_size;
  s << number_of_proposers;
  s << dag_blocks_size;
  s << ghost_path_move_back;
  s << gas_limit;

  return s.out();
}

}  // namespace ebla