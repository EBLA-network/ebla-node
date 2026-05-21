#pragma once

#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <rocksdb/write_batch.h>

#include <filesystem>
#include <functional>
#include <regex>

#include "common/types.hpp"
#include "dag/dag_block.hpp"
#include "logger/logger.hpp"
#include "pbft/pbft_block.hpp"
#include "pbft/period_data.hpp"
#include "storage/uint_comparator.hpp"
#include "transaction/receipt.hpp"
#include "transaction/transaction.hpp"
#include "vote_manager/verified_votes.hpp"

namespace ebla {
namespace fs = std::filesystem;
struct SortitionParamsChange;
// EBLA DB_ROADMAP_v01 §2.4 - fwd decl; full definition in config/config.hpp.
// Used by the DBConfig-accepting DbStorage constructor below. Forward-declared
// (rather than #included) to keep storage.hpp from gaining a new dep on the
// libraries/config/ public surface.
struct DBConfig;

enum StatusDbField : uint8_t {
  ExecutedBlkCount = 0,
  ExecutedTrxCount,
  TrxCount,
  DagBlkCount,
  DagEdgeCount,
  DbMajorVersion,
  DbMinorVersion
};

enum class PbftMgrField : uint8_t { Round = 0, Step };

enum PbftMgrStatus : uint8_t {
  ExecutedBlock = 0,
  ExecutedInRound,
  NextVotedSoftValue,
  NextVotedNullBlockHash,
};

enum class DBMetaKeys { LAST_NUMBER = 1 };

class DbException : public std::exception {
 public:
  explicit DbException(const std::string& desc) : desc_(desc) {}
  virtual ~DbException() = default;

  DbException(const DbException&) = default;
  DbException(DbException&&) = default;
  DbException& operator=(const DbException&) = delete;
  DbException& operator=(DbException&&) = delete;

  virtual const char* what() const noexcept { return desc_.c_str(); }

 private:
  const std::string desc_;
};

using Batch = rocksdb::WriteBatch;
using Slice = rocksdb::Slice;
using OnEntry = std::function<void(Slice const&, Slice const&)>;

class DbStorage : public std::enable_shared_from_this<DbStorage> {
 public:
  class Column {
    std::string const name_;

   public:
    size_t const ordinal_;
    const rocksdb::Comparator* comparator_;
    // EBLA DB_ROADMAP_v01 §2.3 - true if this CF should be placed on the cold
    // (HDD) tier via cf_paths when db_tiering_enabled. Marked at static init
    // time via the COLUMN_TIERED / COLUMN_TIERED_W_COMP macros below; never
    // mutated. Consumed by DbStorage::applyCfOptionsForColumn() in storage.cpp.
    bool const tiered_;

    Column(std::string name, size_t ordinal, const rocksdb::Comparator* comparator, bool tiered)
        : name_(std::move(name)), ordinal_(ordinal), comparator_(comparator), tiered_(tiered) {}

    auto const& name() const { return ordinal_ ? name_ : rocksdb::kDefaultColumnFamilyName; }
  };

  class Columns {
    static inline std::vector<Column> all_;

   public:
    static inline auto const& all = all_;

// EBLA DB_ROADMAP_v01 §2.3 - Four macros now (TIERED variants added).
// COLUMN / COLUMN_W_COMP register a CF that always lives on the hot (SSD) tier.
// COLUMN_TIERED / COLUMN_TIERED_W_COMP register a CF whose bottommost SSTs
// migrate to the cold (HDD) tier when db_tiering_enabled=true. The 4th arg to
// the Column constructor is the tiered flag - supplied by the macros below.
#define COLUMN(__name__) static inline auto const __name__ = all_.emplace_back(#__name__, all_.size(), nullptr, false)
#define COLUMN_TIERED(__name__) \
  static inline auto const __name__ = all_.emplace_back(#__name__, all_.size(), nullptr, true)
#define COLUMN_W_COMP(__name__, ...) \
  static inline auto const __name__ = all_.emplace_back(#__name__, all_.size(), __VA_ARGS__, false)
#define COLUMN_TIERED_W_COMP(__name__, ...) \
  static inline auto const __name__ = all_.emplace_back(#__name__, all_.size(), __VA_ARGS__, true)

    // do not change/move
    COLUMN(default_column);
    // migrations
    COLUMN(migrations);
    // EBLA DB_ROADMAP_v01 §2.3 - TIERED: write-monotonic period data, rarely re-read once finalized.
    // Contains full data for an executed PBFT block including PBFT block, cert votes, dag blocks and transactions
    COLUMN_TIERED_W_COMP(period_data, getIntComparator<PbftPeriod>());
    COLUMN(genesis);
    COLUMN(dag_blocks);
    COLUMN_W_COMP(dag_blocks_level, getIntComparator<uint64_t>());
    // EBLA DB_ROADMAP_v01 §2.3 - TIERED: transaction bodies, RPC-read-only after finalization.
    COLUMN_TIERED(transactions);
    // EBLA DB_ROADMAP_v01 §2.3 - TIERED: trx_hash → period index. Mempool dedup hits this via
    // KeyMayExist (bloom-cached, no disk seek) so cold tier is safe. See Step 3 §3.1.
    COLUMN_TIERED(trx_period);
    COLUMN(status);
    COLUMN(pbft_mgr_round_step);
    COLUMN(pbft_mgr_status);
    COLUMN(cert_voted_block_in_round);  // Cert voted block + round -> node voted for this block
    COLUMN(proposed_pbft_blocks);       // Proposed pbft blocks
    COLUMN(pbft_head);
    COLUMN(latest_round_own_votes);            // own votes of any type for the latest round
    COLUMN(latest_round_five_of_eight_votes);  // 5/8 votes bundles of any type for the latest round
    COLUMN(extra_reward_votes);                // extra reward votes on top of 5/8 cert votes bundle from
                                               // latest_round_five_of_eight_votes
    COLUMN(pbft_block_period);
    // EBLA DB_ROADMAP_v01 §2.3 - TIERED: dag_block → period index, RPC-read path only.
    COLUMN_TIERED(dag_block_period);
    COLUMN_W_COMP(proposal_period_levels_map, getIntComparator<uint64_t>());
    COLUMN(final_chain_meta);
    // EBLA DB_ROADMAP_v01 §2.3 - TIERED: eth_getBlockByNumber payload.
    COLUMN_TIERED(final_chain_blk_by_number);
    // EBLA DB_ROADMAP_v01 §2.3 - TIERED: eth_getBlockByHash → number lookup.
    COLUMN_TIERED(final_chain_blk_hash_by_number);
    // EBLA DB_ROADMAP_v01 §2.3 - TIERED: number → hash reverse lookup.
    COLUMN_TIERED(final_chain_blk_number_by_hash);
    // EBLA DB_ROADMAP_v01 §2.3 - TIERED: eth_getTransactionReceipt path.
    COLUMN_TIERED(final_chain_receipt_by_trx_hash);
    // EBLA DB_ROADMAP_v01 §2.3 - TIERED: eth_getLogs bloom index. WARNING: wide-range
    // eth_getLogs queries on tiered nodes hit the cold tier heavily - see Step 3 §3.2.
    COLUMN_TIERED(final_chain_log_blooms_index);
    COLUMN_W_COMP(sortition_params_change, getIntComparator<PbftPeriod>());

    COLUMN_W_COMP(block_rewards_stats, getIntComparator<uint64_t>());

    // system transactions that is not a part of the block
    COLUMN(system_transaction);
    // system transactions hashes by period
    COLUMN(period_system_transactions);
    // EBLA DB_ROADMAP_v01 §2.3 - TIERED: receipts indexed by period (companion to
    // final_chain_receipt_by_trx_hash; both must be tiered together).
    // final chain receipts by period
    COLUMN_TIERED_W_COMP(final_chain_receipt_by_period, getIntComparator<PbftPeriod>());

#undef COLUMN
#undef COLUMN_TIERED
#undef COLUMN_W_COMP
#undef COLUMN_TIERED_W_COMP
  };

  auto handle(Column const& col) const { return handles_[col.ordinal_]; }

  void DeleteRange(const Column& col, uint64_t begin, uint64_t end);
  void CompactRange(const Column& col, uint64_t begin, uint64_t end);
  rocksdb::ReadOptions read_options_;

  rocksdb::WriteOptions async_write_;
  rocksdb::WriteOptions sync_write_;

 private:
  fs::path path_;
  fs::path db_path_;
  fs::path state_db_path_;
  const std::string kDbDir = "db";
  const std::string kStateDbDir = "state_db";
  std::unique_ptr<rocksdb::DB> db_;
  std::vector<rocksdb::ColumnFamilyHandle*> handles_;
  std::mutex dag_blocks_mutex_;
  bool compression_enabled_ = true;  // LZ4 compression toggle for all column families
  std::atomic<uint64_t> dag_blocks_count_;
  std::atomic<uint64_t> dag_edge_count_;
  const uint32_t kDbSnapshotsEachNblock = 0;
  std::atomic<bool> snapshots_enabled_ = true;
  const uint32_t kDbSnapshotsMaxCount = 0;
  std::set<PbftPeriod> snapshots_;
  uint64_t earliest_block_number_ = 0;

  uint32_t kMajorVersion_;
  bool major_version_changed_ = false;
  bool minor_version_changed_ = false;

  // EBLA DB_ROADMAP_v01 §2.4 - Tiered storage & RAM-tunable state.
  // Defaults are "off / use RocksDB defaults" so the 8-arg back-compat
  // constructor produces behavior identical to a pre-roadmap binary. The
  // DBConfig-accepting constructor populates these via the member-init list
  // BEFORE openDb() runs, so the values are live when CFs are first opened.
  bool tiering_enabled_ = false;
  fs::path db_archive_path_;
  uint64_t db_hot_size_limit_bytes_ = 0;
  uint8_t cold_compression_level_ = 9;
  uint64_t block_cache_size_bytes_ = 0;
  uint64_t write_buffer_size_bytes_ = 0;
  // [hot path (db_path_), cold path (db_archive_path_)] when tiering enabled.
  // Lifetime attached to *this; each tiered ColumnFamilyOptions takes a copy.
  std::vector<rocksdb::DbPath> tiered_paths_;
  // Shared block cache across all CFs. Constructed once at openDb() iff
  // block_cache_size_bytes_ > 0; held here so the shared_ptr stays alive for
  // the DB's lifetime. (Forward decl of rocksdb::Cache is sufficient for the
  // member; storage.cpp will include <rocksdb/cache.h> for construction.)
  std::shared_ptr<rocksdb::Cache> block_cache_;

  // EBLA DB_ROADMAP_v01 §2.4 - Shared DB-open logic. Called from BOTH
  // constructors after the member-init list has populated path_, db_path_,
  // state_db_path_, compression_enabled_, and (in the new constructor) all
  // tiering/RAM fields.
  void openDb(uint32_t max_open_files, PbftPeriod db_revert_to_period, addr_t node_addr, bool rebuild);

  // EBLA DB_ROADMAP_v01 §4.3 - Single source of truth for ColumnFamilyOptions
  // construction. Called from FOUR call sites in storage.cpp:
  //   1. Main descriptor builder in openDb()
  //   2. rebuildColumns() per-CF descriptor builder
  //   3. deleteColumnData() CF re-creation
  //   4. copyColumn() CF re-creation
  // Missing one of these sites causes silent loss of tiering on rebuild/recreate.
  void applyCfOptionsForColumn(rocksdb::ColumnFamilyOptions& opts, const Column& col) const;

  LOG_OBJECTS_DEFINE

 public:
  explicit DbStorage(fs::path const& base_path, uint32_t db_snapshot_each_n_pbft_block = 0, uint32_t max_open_files = 0,
                     uint32_t db_max_snapshots = 0, PbftPeriod db_revert_to_period = 0, addr_t node_addr = addr_t(),
                     bool rebuild = false, bool enable_compression = true);

  // EBLA DB_ROADMAP_v01 §2.4 — DBConfig-accepting constructor (preferred for
  // new callers). Unpacks all DBConfig fields including the tiering/RAM ones
  // and delegates the actual DB open to the same openDb() helper as the 8-arg
  // back-compat constructor above. Existing callers (tests, current app.cpp
  // before file 6 lands) keep working with the back-compat constructor.
  DbStorage(fs::path const& base_path, const DBConfig& cfg, addr_t node_addr = addr_t(), bool rebuild = false);

  ~DbStorage();

  DbStorage(const DbStorage&) = delete;
  DbStorage(DbStorage&&) = delete;
  DbStorage& operator=(const DbStorage&) = delete;
  DbStorage& operator=(DbStorage&&) = delete;

  auto const& path() const { return path_; }
  auto dbStoragePath() const { return db_path_; }
  auto stateDbStoragePath() const { return state_db_path_; }
  static Batch createWriteBatch();
  void commitWriteBatch(Batch& write_batch, const rocksdb::WriteOptions& opts);
  void commitWriteBatch(Batch& write_batch) { commitWriteBatch(write_batch, async_write_); }

  void rebuildColumns(const rocksdb::Options& options);
  bool createSnapshot(PbftPeriod period);
  void deleteSnapshot(PbftPeriod period);
  void recoverToPeriod(PbftPeriod period);
  void loadSnapshots();
  void disableSnapshots();
  void enableSnapshots();
  void updateDbVersions();
  void deleteColumnData(const Column& c);

  void replaceColumn(const Column& to_be_replaced_col, std::unique_ptr<rocksdb::ColumnFamilyHandle>&& replacing_col);
  std::unique_ptr<rocksdb::ColumnFamilyHandle> copyColumn(rocksdb::ColumnFamilyHandle* orig_column,
                                                          const std::string& new_col_name, bool move_data = false);

  // For removal of LOG.old.* files in the database
  void removeTempFiles() const;
  void removeFilesWithPattern(const std::string& directory, const std::regex& pattern) const;
  void deleteTmpDirectories(const std::string& path) const;

  uint32_t getMajorVersion() const;
  std::unique_ptr<rocksdb::Iterator> getColumnIterator(const Column& c);
  std::unique_ptr<rocksdb::Iterator> getColumnIterator(rocksdb::ColumnFamilyHandle* c);

  // Genesis
  void setGenesisHash(const h256& genesis_hash);
  std::optional<h256> getGenesisHash();

  // Period data
  void savePeriodData(const PeriodData& period_data, Batch& write_batch);
  dev::bytes getPeriodDataRaw(PbftPeriod period) const;
  std::optional<PeriodData> getPeriodData(PbftPeriod period) const;
  std::optional<PbftBlock> getPbftBlock(PbftPeriod period) const;
  std::vector<std::shared_ptr<PbftVote>> getPeriodCertVotes(PbftPeriod period) const;
  blk_hash_t getPeriodBlockHash(PbftPeriod period) const;
  SharedTransactions transactionsFromPeriodDataRlp(PbftPeriod period, const dev::RLP& period_data_rlp) const;
  std::optional<SharedTransactions> getPeriodTransactions(PbftPeriod period) const;
  uint64_t getEarliestBlockNumber() const;

  // DAG
  void saveDagBlock(const std::shared_ptr<DagBlock>& blk, Batch* write_batch_p = nullptr);
  std::shared_ptr<DagBlock> getDagBlock(blk_hash_t const& hash);
  bool dagBlockInDb(blk_hash_t const& hash);
  std::set<blk_hash_t> getBlocksByLevel(level_t level);
  level_t getLastBlocksLevel() const;
  std::vector<std::shared_ptr<DagBlock>> getDagBlocksAtLevel(level_t level, int number_of_levels);
  void updateDagBlockCounters(std::vector<std::shared_ptr<DagBlock>> blks);
  std::map<level_t, std::vector<std::shared_ptr<DagBlock>>> getNonfinalizedDagBlocks();
  void removeDagBlockBatch(Batch& write_batch, blk_hash_t const& hash);
  void removeDagBlock(blk_hash_t const& hash);
  // Sortition params
  void saveSortitionParamsChange(PbftPeriod period, const SortitionParamsChange& params, Batch& batch);
  std::deque<SortitionParamsChange> getLastSortitionParams(size_t count);
  std::optional<SortitionParamsChange> getParamsChangeForPeriod(PbftPeriod period);

  // Transaction
  std::shared_ptr<Transaction> getTransaction(trx_hash_t const& hash) const;
  std::shared_ptr<Transaction> getTransaction(PbftPeriod period, uint32_t position) const;

  SharedTransactions getAllNonfinalizedTransactions();
  bool transactionInDb(trx_hash_t const& hash);
  bool transactionFinalized(trx_hash_t const& hash);
  std::vector<bool> transactionsInDb(std::vector<trx_hash_t> const& trx_hashes);
  std::vector<bool> transactionsFinalized(std::vector<trx_hash_t> const& trx_hashes);
  void addTransactionToBatch(Transaction const& trx, Batch& write_batch);
  void removeTransactionToBatch(trx_hash_t const& trx, Batch& write_batch);

  void addTransactionLocationToBatch(Batch& write_batch, trx_hash_t const& trx, PbftPeriod period, uint32_t position,
                                     bool is_system = false);
  std::optional<TransactionLocation> getTransactionLocation(trx_hash_t const& hash) const;
  std::unordered_map<trx_hash_t, PbftPeriod> getAllTransactionPeriod();
  uint64_t getTransactionCount(PbftPeriod period) const;
  SharedTransactionReceipts getBlockReceipts(PbftPeriod period) const;
  std::optional<TransactionReceipt> getTransactionReceipt(EthBlockNumber blk_n, uint64_t position) const;

  /**
   * @brief Gets finalized transactions from provided hashes
   *
   * @param trx_hashes
   *
   * @return Returns transactions
   */
  SharedTransactions getFinalizedTransactions(std::vector<trx_hash_t> const& trx_hashes) const;

  // System transaction
  void addSystemTransactionToBatch(Batch& write_batch, SharedTransaction trx);
  std::shared_ptr<Transaction> getSystemTransaction(const trx_hash_t& hash) const;
  void addPeriodSystemTransactions(Batch& write_batch, SharedTransactions trxs, PbftPeriod period);
  std::vector<trx_hash_t> getPeriodSystemTransactionsHashes(PbftPeriod period) const;
  SharedTransactions getPeriodSystemTransactions(PbftPeriod period) const;

  // PBFT manager
  uint32_t getPbftMgrField(PbftMgrField field);
  void savePbftMgrField(PbftMgrField field, uint32_t value);
  void addPbftMgrFieldToBatch(PbftMgrField field, uint32_t value, Batch& write_batch);

  bool getPbftMgrStatus(PbftMgrStatus field);
  void savePbftMgrStatus(PbftMgrStatus field, bool const& value);
  void addPbftMgrStatusToBatch(PbftMgrStatus field, bool const& value, Batch& write_batch);

  void saveCertVotedBlockInRound(PbftRound round, const std::shared_ptr<PbftBlock>& block);
  std::optional<std::pair<PbftRound, std::shared_ptr<PbftBlock>>> getCertVotedBlockInRound() const;
  void removeCertVotedBlockInRound(Batch& write_batch);

  // pbft_blocks
  std::optional<PbftBlock> getPbftBlock(blk_hash_t const& hash);
  bool pbftBlockInDb(blk_hash_t const& hash);

  // Proposed pbft blocks
  void saveProposedPbftBlock(const std::shared_ptr<PbftBlock>& block);
  void removeProposedPbftBlock(const blk_hash_t& block_hash, Batch& write_batch);
  std::vector<std::shared_ptr<PbftBlock>> getProposedPbftBlocks();

  // pbft_blocks (head)
  std::string getPbftHead(blk_hash_t const& hash);
  void savePbftHead(blk_hash_t const& hash, std::string const& pbft_chain_head_str);
  void addPbftHeadToBatch(ebla::blk_hash_t const& head_hash, std::string const& head_str, Batch& write_batch);

  // status
  uint64_t getStatusField(StatusDbField const& field);
  void saveStatusField(StatusDbField const& field, uint64_t value);
  void addStatusFieldToBatch(StatusDbField const& field, uint64_t value, Batch& write_batch);

  // Own votes for the latest round
  void saveOwnVerifiedVote(const std::shared_ptr<PbftVote>& vote);
  std::vector<std::shared_ptr<PbftVote>> getOwnVerifiedVotes();
  void clearOwnVerifiedVotes(Batch& write_batch, const std::vector<std::shared_ptr<PbftVote>>& own_verified_votes);

  // 5/8 votes bundles for the latest round
  void replaceFiveOfEightVotes(FiveOfEightVotedBlockType type, const std::vector<std::shared_ptr<PbftVote>>& votes);
  void replaceFiveOfEightVotesToBatch(FiveOfEightVotedBlockType type,
                                      const std::vector<std::shared_ptr<PbftVote>>& votes, Batch& write_batch);
  std::vector<std::shared_ptr<PbftVote>> getAllFiveOfEightVotes();

  // Reward votes - cert votes for the latest finalized block
  void removeExtraRewardVotes(const std::vector<vote_hash_t>& votes, Batch& write_batch);
  void saveExtraRewardVote(const std::shared_ptr<PbftVote>& vote);
  std::vector<std::shared_ptr<PbftVote>> getRewardVotes();

  // period_pbft_block
  void addPbftBlockPeriodToBatch(PbftPeriod period, ebla::blk_hash_t const& pbft_block_hash, Batch& write_batch);
  std::pair<bool, PbftPeriod> getPeriodFromPbftHash(ebla::blk_hash_t const& pbft_block_hash);
  // dag_block_period
  std::shared_ptr<std::pair<PbftPeriod, uint32_t>> getDagBlockPeriod(blk_hash_t const& hash);
  void addDagBlockPeriodToBatch(blk_hash_t const& hash, PbftPeriod period, uint32_t position, Batch& write_batch);

  uint64_t getDagBlocksCount() const { return dag_blocks_count_.load(); }
  uint64_t getDagEdgeCount() const { return dag_edge_count_.load(); }

  auto getNumTransactionExecuted() { return getStatusField(StatusDbField::ExecutedTrxCount); }
  auto getNumTransactionInDag() { return getStatusField(StatusDbField::TrxCount); }
  auto getNumBlockExecuted() { return getStatusField(StatusDbField::ExecutedBlkCount); }

  std::vector<blk_hash_t> getFinalizedDagBlockHashesByPeriod(PbftPeriod period);
  std::vector<std::shared_ptr<DagBlock>> getFinalizedDagBlockByPeriod(PbftPeriod period);
  std::pair<blk_hash_t, std::vector<std::shared_ptr<DagBlock>>> getLastPbftBlockHashAndFinalizedDagBlockByPeriod(
      PbftPeriod period);

  // DPOS level to proposal period map
  std::optional<uint64_t> getProposalPeriodForDagLevel(uint64_t level);
  void saveProposalPeriodDagLevelsMap(uint64_t level, PbftPeriod period);
  void addProposalPeriodDagLevelsMapToBatch(uint64_t level, PbftPeriod period, Batch& write_batch);

  bool hasMinorVersionChanged() { return minor_version_changed_; }
  bool hasMajorVersionChanged() { return major_version_changed_; }

  void compactColumn(Column const& column) { db_->CompactRange({}, handle(column), nullptr, nullptr); }

  inline static bytes asBytes(std::string const& b) {
    return bytes((byte const*)b.data(), (byte const*)(b.data() + b.size()));
  }

  template <typename T>
  inline static Slice make_slice(T const* begin, size_t size) {
    if (!size) {
      return {};
    }
    return {reinterpret_cast<char const*>(begin), size};
  }

  inline static Slice toSlice(dev::bytesConstRef const& b) { return make_slice(b.data(), b.size()); }

  template <unsigned N>
  inline static Slice toSlice(dev::FixedHash<N> const& h) {
    return make_slice(h.data(), N);
  }

  inline static Slice toSlice(dev::bytes const& b) { return make_slice(b.data(), b.size()); }

  template <class N>
  inline static auto toSlice(N const& n) -> std::enable_if_t<std::is_integral_v<N> || std::is_enum_v<N>, Slice> {
    return make_slice(&n, sizeof(N));
  }

  inline static Slice toSlice(std::string const& str) { return make_slice(str.data(), str.size()); }

  inline static auto const& toSlice(Slice const& s) { return s; }

  template <typename T>
  inline static auto toSlices(std::vector<T> const& keys) {
    std::vector<Slice> ret;
    ret.reserve(keys.size());
    for (auto const& k : keys) {
      ret.emplace_back(toSlice(k));
    }
    return ret;
  }

  inline static auto const& toSlices(std::vector<Slice> const& ss) { return ss; }

  template <typename K>
  std::string lookup(K const& key, Column const& column) const {
    std::string value;
    auto status = db_->Get(read_options_, handle(column), toSlice(key), &value);
    if (status.IsNotFound()) {
      return value;
    }
    checkStatus(status);
    return value;
  }

  template <typename Int, typename K>
  auto lookup_int(K const& key, Column const& column) -> std::enable_if_t<std::is_integral_v<Int>, std::optional<Int>> {
    auto str = lookup(key, column);
    if (str.empty()) {
      return std::nullopt;
    }
    return *reinterpret_cast<Int*>(str.data());
  }

  template <typename K>
  bool exist(K const& key, Column const& column) {
    std::string value;
    // KeyMayExist can lead to a few false positives, but not false negatives.
    if (db_->KeyMayExist(read_options_, handle(column), toSlice(key), &value)) {
      auto status = db_->Get(read_options_, handle(column), toSlice(key), &value);
      if (status.IsNotFound()) {
        return false;
      }
      checkStatus(status);
      return !value.empty();
    }
    return false;
  }

  static void checkStatus(rocksdb::Status const& status);

  template <typename K, typename V>
  void insert(rocksdb::ColumnFamilyHandle* col, const K& k, const V& v) {
    checkStatus(db_->Put(async_write_, col, toSlice(k), toSlice(v)));
  }

  template <typename K, typename V>
  void insert(Column const& col, K const& k, V const& v) {
    checkStatus(db_->Put(async_write_, handle(col), toSlice(k), toSlice(v)));
  }

  template <typename K, typename V>
  void insert(Batch& batch, Column const& col, K const& k, V const& v) {
    checkStatus(batch.Put(handle(col), toSlice(k), toSlice(v)));
  }

  template <typename K, typename V>
  void insert(Batch& batch, rocksdb::ColumnFamilyHandle* col, K const& k, V const& v) {
    checkStatus(batch.Put(col, toSlice(k), toSlice(v)));
  }

  template <typename K>
  void remove(Column const& col, K const& k) {
    checkStatus(db_->Delete(async_write_, handle(col), toSlice(k)));
  }

  template <typename K>
  void remove(Batch& batch, Column const& col, K const& k) {
    checkStatus(batch.Delete(handle(col), toSlice(k)));
  }

  template <typename K>
  void remove(Batch& batch, Column const& col, std::unordered_set<K> const& keys) {
    for (auto const& k : keys) {
      checkStatus(batch.Delete(handle(col), toSlice(k)));
    }
  }

  template <typename T>
  void clearColumnHistory(std::unordered_set<T>& to_keep, Column c) {
    std::map<T, bytes> data_to_keep;
    for (auto t : to_keep) {
      auto raw = asBytes(lookup(t, c));
      if (!raw.empty()) {
        data_to_keep[t] = raw;
      }
    }

    deleteColumnData(c);
    auto batch = createWriteBatch();
    for (auto data : data_to_keep) {
      insert(batch, c, data.first, data.second);
    }
    commitWriteBatch(batch);
    data_to_keep.clear();
  }

  void forEach(Column const& col, OnEntry const& f);
};

}  // namespace ebla
