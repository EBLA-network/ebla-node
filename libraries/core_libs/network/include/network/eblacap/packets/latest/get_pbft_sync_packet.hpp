#pragma once

#include "common/encoding_rlp.hpp"

namespace ebla::network::eblacap {

struct GetPbftSyncPacket {
  size_t height_to_sync;

  RLP_FIELDS_DEFINE_INPLACE(height_to_sync)
};

}  // namespace ebla::network::eblacap
