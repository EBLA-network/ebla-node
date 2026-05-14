// libraries/config/src/protocol_config.cpp
#include "config/protocol_config.hpp"

#include "common/config_exception.hpp"

namespace ebla {

Json::Value enc_json(const SlashingConfig& obj) {
  Json::Value json(Json::objectValue);
  json["jail_time"] = dev::toJS(obj.jail_time);
  return json;
}

void dec_json(const Json::Value& json, SlashingConfig& obj) { obj.jail_time = dev::getUInt(json["jail_time"]); }
RLP_FIELDS_DEFINE(SlashingConfig, jail_time)

Json::Value enc_json(const SupplyConfig& obj) {
  Json::Value json(Json::objectValue);
  json["max_supply"] = dev::toJS(obj.max_supply);
  json["generated_rewards"] = dev::toJS(obj.generated_rewards);
  return json;
}

void dec_json(const Json::Value& json, SupplyConfig& obj) {
  obj.max_supply = dev::jsToU256(json["max_supply"].asString());
  obj.generated_rewards = dev::jsToU256(json["generated_rewards"].asString());
}
RLP_FIELDS_DEFINE(SupplyConfig, max_supply, generated_rewards)

Json::Value enc_json(const ProtocolConfig& obj) {
  Json::Value json(Json::objectValue);

  auto& rewards = json["rewards_distribution_frequency"];
  rewards = Json::objectValue;
  for (auto i = obj.rewards_distribution_frequency.begin(); i != obj.rewards_distribution_frequency.end(); ++i) {
    rewards[std::to_string(i->first)] = i->second;
  }

  json["slashing"] = enc_json(obj.slashing);
  json["supply"] = enc_json(obj.supply);

  return json;
}

void dec_json(const Json::Value& json, ProtocolConfig& obj) {
  if (const auto& e = json["rewards_distribution_frequency"]) {
    assert(e.isObject());

    for (auto itr = e.begin(); itr != e.end(); ++itr) {
      obj.rewards_distribution_frequency[dev::getUInt(itr.key())] = dev::getUInt(*itr);
    }
  }

  dec_json(json["slashing"], obj.slashing);
  dec_json(json["supply"], obj.supply);
}

RLP_FIELDS_DEFINE(ProtocolConfig, rewards_distribution_frequency, slashing, supply)

}  // namespace ebla