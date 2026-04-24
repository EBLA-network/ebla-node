#include "config/hardfork.hpp"

#include "common/config_exception.hpp"

namespace ebla {

Json::Value enc_json(const SlashingConfig& obj) {
  Json::Value json(Json::objectValue);
  json["jail_time"] = dev::toJS(obj.jail_time);
  return json;
}

void dec_json(const Json::Value& json, SlashingConfig& obj) { obj.jail_time = dev::getUInt(json["jail_time"]); }
RLP_FIELDS_DEFINE(SlashingConfig, jail_time)

Json::Value enc_json(const AspenHardfork& obj) {
  Json::Value json(Json::objectValue);
  json["max_supply"] = dev::toJS(obj.max_supply);
  json["generated_rewards"] = dev::toJS(obj.generated_rewards);
  return json;
}

void dec_json(const Json::Value& json, AspenHardfork& obj) {
  obj.max_supply = dev::jsToU256(json["max_supply"].asString());
  obj.generated_rewards = dev::jsToU256(json["generated_rewards"].asString());
}
RLP_FIELDS_DEFINE(AspenHardfork, max_supply, generated_rewards)

Json::Value enc_json(const HardforksConfig& obj) {
  Json::Value json(Json::objectValue);
  json["initial_validators"] = Json::Value(Json::arrayValue);

  auto& rewards = json["rewards_distribution_frequency"];
  rewards = Json::objectValue;
  for (auto i = obj.rewards_distribution_frequency.begin(); i != obj.rewards_distribution_frequency.end(); ++i) {
    rewards[std::to_string(i->first)] = i->second;
  }

  json["slashing"] = enc_json(obj.slashing);
  json["aspen_hf"] = enc_json(obj.aspen_hf);
  // json["bamboo_hf"] = enc_json(obj.bamboo_hf);

  return json;
}

void dec_json(const Json::Value& json, HardforksConfig& obj) {
  if (const auto& e = json["rewards_distribution_frequency"]) {
    assert(e.isObject());

    for (auto itr = e.begin(); itr != e.end(); ++itr) {
      obj.rewards_distribution_frequency[dev::getUInt(itr.key())] = dev::getUInt(*itr);
    }
  }

  dec_json(json["slashing"], obj.slashing);
  dec_json(json["aspen_hf"], obj.aspen_hf);
  // dec_json(json["bamboo_hf"], obj.bamboo_hf);
}

RLP_FIELDS_DEFINE(HardforksConfig, rewards_distribution_frequency, slashing, aspen_hf)
}  // namespace ebla