#pragma once

#include <jsonrpccpp/common/exception.h>
#include <jsonrpccpp/server.h>
#include <libdevcore/Common.h>

#include <memory>

#include "EblaFace.h"
#include "common/app_base.hpp"
#include "libweb3jsonrpc/ModularServer.h"

namespace ebla::net {

class Ebla : public EblaFace {
 public:
  explicit Ebla(std::shared_ptr<ebla::AppBase> app);

  virtual RPCModules implementedModules() const override { return RPCModules{RPCModule{"ebla", "1.0"}}; }

  virtual std::string ebla_protocolVersion() override;
  virtual Json::Value ebla_getVersion() override;
  virtual Json::Value ebla_getDagBlockByHash(const std::string& _blockHash, bool _includeTransactions) override;
  virtual Json::Value ebla_getDagBlockByLevel(const std::string& _blockLevel, bool _includeTransactions) override;
  virtual std::string ebla_dagBlockLevel() override;
  virtual std::string ebla_dagBlockPeriod() override;
  virtual Json::Value ebla_getScheduleBlockByPeriod(const std::string& _period) override;
  virtual Json::Value ebla_getNodeVersions() override;
  virtual std::string ebla_pbftBlockHashByPeriod(const std::string& _period) override;
  virtual Json::Value ebla_getConfig() override;
  virtual Json::Value ebla_getChainStats() override;
  virtual std::string ebla_yield(const std::string& _period) override;
  virtual std::string ebla_totalSupply(const std::string& _period) override;

 protected:
  std::weak_ptr<ebla::AppBase> app_;

 private:
  Json::Value version;

  std::shared_ptr<ebla::AppBase> tryGetApp();
};

}  // namespace ebla::net
