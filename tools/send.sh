#!/bin/bash
# Uploads an ELF to Payload Manager (0.6.x) and starts it, without FTP.
#   PS5_HOST=192.168.1.50 tools/send.sh dist/xpad-ps5.elf [--autoload]
# --autoload also appends local/xpad-ps5 to Payload Manager's autoload list.
set -euo pipefail
ELF=${1:?usage: PS5_HOST=<ip> $0 <elf> [--autoload]}
HOST=${PS5_HOST:?set PS5_HOST to the console address}
B=http://$HOST:8084
NAME=$(basename "$ELF")
SLOT=local/${NAME%.elf}

# A running xpad-ps5 answers on :8096; stop it first (refuses if it cannot tell which process it is).
if curl -s -m5 "http://$HOST:8096/" | grep xpad-ps5 >/dev/null; then
  PIDS=$(curl -s -m10 "$B/processes_list" | python3 -c 'import json,sys; [print(p["pid"]) for p in json.load(sys.stdin)["processes"] if p["name"]=="payload.elf"]')
  if [ "$(echo $PIDS | wc -w)" -ne 1 ]; then echo "several payload.elf running: stop xpad-ps5 by hand"; exit 1; fi
  curl -s -m10 -X POST -H "Origin: $B" "$B/process_kill?pid=$PIDS"; echo
  sleep 2
fi

# Payload Manager checks the Origin header on every change.
curl -fsS -m60 -X POST -H "Origin: $B" -H 'Content-Type: application/octet-stream' \
  --data-binary @"$ELF" "$B/manage:upload?filename=$NAME&mode=replace"; echo " uploaded as $SLOT"

if [ "${2:-}" = "--autoload" ]; then
  LIST=$(curl -fsS -m10 "$B/get_config" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("AUTOLOAD_LIST",""))')
  case ",$LIST," in
    *",$SLOT,"*) echo "already in autoload: $LIST" ;;
    *) NEW=${LIST:+$LIST,}$SLOT
       curl -fsS -m10 -X POST -H "Origin: $B" -H 'Content-Type: application/json' \
         -d "{\"AUTOLOAD_LIST\":\"$NEW\"}" "$B/set_config"; echo " autoload: $NEW" ;;
  esac
fi

curl -fsS -m15 -X POST -H "Origin: $B" "$B/loadpayload:$SLOT"; echo " started"
