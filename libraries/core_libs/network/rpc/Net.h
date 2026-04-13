#pragma once

#include "NetFace.h"
#include "common/app_base.hpp"

namespace ebla::net {

class Net : public NetFace {
 public:
  explicit Net(std::shared_ptr<ebla::AppBase> const& app);
  virtual RPCModules implementedModules() const override { return RPCModules{RPCModule{"net", "1.0"}}; }
  virtual std::string net_version() override;
  virtual std::string net_peerCount() override;
  virtual bool net_listening() override;

 private:
  std::weak_ptr<ebla::AppBase> app_;
};

}  // namespace ebla::net
