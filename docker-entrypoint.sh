#!/bin/bash

export EBLA_CONF_PATH=${EBLA_CONF_PATH:=/root/.ebla/conf_ebla.json}
export EBLA_PERSISTENT_PATH=${EBLA_PERSISTENT_PATH:=/root/.ebla}
export EBLA_COPY_COREDUMPS=${EBLA_COPY_COREDUMPS:=true}
export EBLA_SLEEP_DIAGNOSE=${EBLA_SLEEP_DIAGNOSE:=false}

FLAGS=""
if [[ -z "${HOSTNAME}" ]]; then
  echo "HOSTNAME is not set."
else
  INDEX=${HOSTNAME##*-}

  ADVERTISED_IP_NAME="ADVERTISED_IP_$INDEX"
  ADVERTISED_IP=${!ADVERTISED_IP_NAME}
  if [[ -z "${ADVERTISED_IP}" ]]; then
    echo "ADVERTISED_IP is not set."
  else
    if [ "$ADVERTISED_IP" = "auto" ]; then
      ADVERTISED_IP=$(curl icanhazip.com 2>/dev/null)
    fi
    FLAGS="--public-ip ${ADVERTISED_IP}"
  fi

  ADVERTISED_PORT_NAME="ADVERTISED_PORT_$INDEX"
  ADVERTISED_PORT=${!ADVERTISED_PORT_NAME}
  if [[ -z "${ADVERTISED_PORT}" ]]; then
    echo "ADVERTISED_PORT is not set."
  else
    FLAGS="$FLAGS --port ${ADVERTISED_PORT}"
  fi
fi

# EBLA DB_ROADMAP_v01 §Docker-step — DB tunable env-var → CLI-flag bridges.
# Each EBLA_DB_* env var, if non-empty, is forwarded as the corresponding
# --db-* flag. Each flag is optional from eblad's perspective; if the env var
# is unset, the JSON config value is preserved. The C++ parser (file 4b) does
# strict validation, including allow-list path-prefix check on
# EBLA_DB_ARCHIVE_PATH and overflow-safe parsing on size strings.
#
# DB_FLAGS is kept SEPARATE from FLAGS (which carries --public-ip / --port)
# because ebla-bootnode does not understand --db-* flags. Only the eblad
# branches (eblad, join, single) consume DB_FLAGS.
#
# Operator examples:
#   8 GB validator:   EBLA_DB_BLOCK_CACHE_SIZE=512MB EBLA_DB_WRITE_BUFFER_SIZE=512MB
#   16 GB validator:  EBLA_DB_BLOCK_CACHE_SIZE=2GB EBLA_DB_TIERING_ENABLED=true \
#                     EBLA_DB_ARCHIVE_PATH=/mnt/hdd/ebla-archive EBLA_DB_HOT_SIZE_LIMIT=800GB
#   32 GB RPC node:   EBLA_DB_BLOCK_CACHE_SIZE=8GB EBLA_DB_MAX_OPEN_FILES=2048
#
# Security note: env vars are visible via `docker inspect`. Safe for these
# RAM/path tunables (not secrets). NEVER put node_secret/vrf_secret through
# env vars — use Docker/K8s secrets instead.
DB_FLAGS=""
if [[ -n "${EBLA_DB_BLOCK_CACHE_SIZE}" ]]; then
  DB_FLAGS="$DB_FLAGS --db-block-cache-size ${EBLA_DB_BLOCK_CACHE_SIZE}"
fi
if [[ -n "${EBLA_DB_WRITE_BUFFER_SIZE}" ]]; then
  DB_FLAGS="$DB_FLAGS --db-write-buffer-size ${EBLA_DB_WRITE_BUFFER_SIZE}"
fi
if [[ -n "${EBLA_DB_MAX_OPEN_FILES}" ]]; then
  DB_FLAGS="$DB_FLAGS --db-max-open-files ${EBLA_DB_MAX_OPEN_FILES}"
fi
if [[ -n "${EBLA_DB_TIERING_ENABLED}" ]]; then
  DB_FLAGS="$DB_FLAGS --db-tiering-enabled ${EBLA_DB_TIERING_ENABLED}"
fi
if [[ -n "${EBLA_DB_ARCHIVE_PATH}" ]]; then
  DB_FLAGS="$DB_FLAGS --db-archive-path ${EBLA_DB_ARCHIVE_PATH}"
fi
if [[ -n "${EBLA_DB_HOT_SIZE_LIMIT}" ]]; then
  DB_FLAGS="$DB_FLAGS --db-hot-size-limit ${EBLA_DB_HOT_SIZE_LIMIT}"
fi
if [[ -n "${EBLA_DB_COLD_COMPRESSION_LEVEL}" ]]; then
  DB_FLAGS="$DB_FLAGS --db-cold-compression-level ${EBLA_DB_COLD_COMPRESSION_LEVEL}"
fi

case $1 in

  ebla-bootnode)
    echo "Starting ebla-bootnode..."
    ebla-bootnode $FLAGS "${@:2}"
    ;;

  eblad)
    echo "Starting eblad..."
    eblad $FLAGS $DB_FLAGS "${@:2}"
    ;;

  join)
    echo "Starting eblad..."
    eblad $FLAGS $DB_FLAGS \
            --config $EBLA_CONF_PATH \
            --chain-id $2

    ;;

  single)
	  echo "Starting eblad..."
    eblad $FLAGS $DB_FLAGS \
            --config $EBLA_CONF_PATH

    ;;

  exec)
    exec "${@:2}"
    ;;

  *)
    echo "You should choose between:"
    echo "ebla-bootnode, eblad, single, join {NAMED_NETWOTK}"
    ;;

esac

# Hack to copy coredumps on  K8s (gke) current /proc/sys/kernel/core_pattern
if [ "$EBLA_COPY_COREDUMPS" = true ] ; then
    echo "Copying dump (if any) to $EBLA_PERSISTENT_PATH"
    find / -maxdepth 1 -type f -name '*core*' -exec cp -v "{}" $EBLA_PERSISTENT_PATH  \;
fi

# Hack to sleep forever so devs can diagnose the pod on k8s
# We should not set Liveness/Readiness for this to work
if [ "$EBLA_SLEEP_DIAGNOSE" = true ] ; then
    echo "Sleeping forever for diagnosis"
    while true
    do
        echo "Crashed. Waiting on diagnosis..."
        sleep 300
    done
fi
