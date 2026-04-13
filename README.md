# Introducing Ebla

Ebla is a Practical Byzantine Fault Tolerance blockchain.


# Whitepaper
You can read the Ebla Whitepaper at https://www.ebla.io/whitepaper.


# Quickstart
Just want to get up and running quickly? We have pre-built docker images for your convenience.
More details are in our [quickstart guide](doc/quickstart_guide.md).


# Downloading
There are 2 options how to run the latest version of ebla-node:

### Docker image
Download and run ebla docker image with pre-installed eblad binary [here](https://hub.docker.com/r/ebla/ebla-node).

### Ubuntu binary
Download and run statically linked eblad binary [here](https://github.com/EBLA-network/ebla-node/releases).


# Building
If you would like to build from source, we do have [build instructions](doc/building.md) for Linux (Ubuntu LTS) and macOS.


# Running

### Inside docker image
    eblad --conf_ebla /etc/eblad/eblad.conf

### Pre-built binary or manual build:
    ./eblad --conf_ebla /path/to/config/file


# Contributing
Want to contribute to Ebla repository ? We in Ebla highly appreciate community work so if you feel like you want to
participate you are more than welcome. You can start by reading [contributing tutorial](doc/contributing.md).


# Useful doc
- [Git practices](doc/git_practices.md)
- [Coding practices](doc/coding_practices.md)
- [EVM incompatibilities](doc/evm_incompatibilities.md)
