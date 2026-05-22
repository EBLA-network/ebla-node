# EVM incompatibilities

## Unsupported EIPs
Following EIPs are not supported by our EVM:
- [EIP-1559](https://eips.ethereum.org/EIPS/eip-1559)
- [EIP-4844](https://eips.ethereum.org/EIPS/eip-4844)
- [EIP-2718](https://eips.ethereum.org/EIPS/eip-2718) — *partial support*: the JSON-RPC presentation layer is EIP-2718-aware (transaction and receipt responses include a `"type"` field, hardcoded to `"0x0"` for legacy transactions). However, **acceptance of typed-transaction envelopes** (`0x01`-prefixed EIP-2930 access-list transactions, `0x02`-prefixed EIP-1559 dynamic-fee transactions) is **not** supported — EBLA still rejects such envelopes at `eth_sendRawTransaction` and the p2p layer. Submit legacy transactions only.

## Latest supported solc version
We currently support `0.8.26`.

## go-ethereum library
If you are having issue with go-ethereum library, please use our slightly modified version https://github.com/EBLA-network/go-ethereum

# Nonce handling
Due to nature of DAG which can reorder transactions: 
- Nonce skipping is possible meaning any transaction with nonce larger than previous nonce can be executed.
- It is possible for a transaction without enough balance to pay gas to reach EVM and execution. These transactions will increase the account nonce.
