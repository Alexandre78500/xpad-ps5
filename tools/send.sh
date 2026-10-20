#!/bin/bash
# Uploads an ELF to Payload Manager (0.6.x) and starts it, without FTP.
#   PS5_HOST=192.168.1.50 tools/send.sh dist/xpad-ps5.elf [--autoload]
# --autoload also appends local/xpad-ps5 to Payload Manager's autoload list.
set -euo pipefail
ELF=${1:?usage: PS5_HOST=<ip> $0 <elf> [--autoload]}
HOST=${PS5_HOST:?set PS5_HOST to the console address}
B=http://$HOST:8084
NAME=$(basename "$ELF")

# A running xpad-ps5 holds port 8096 (its lock) and shows up as the one payload.elf; stop it first and wait
# until the process is really gone. Refuses if it cannot tell which process it is.
if nc -z -w 3 "$HOST" 8096 2>/dev/null; then
  PIDS=$(curl -s -m10 "$B/processes_list" | python3 -c 'import json,sys; [print(p["pid"]) for p in json.load(sys.stdin)["processes"] if p["name"]=="payload.elf"]')
  if [ "$(echo $PIDS | wc -w)" -ne 1 ]; then echo "port 8096 busy and not exactly one payload.elf: stop xpad-ps5 by hand"; exit 1; fi
  curl -s -m10 -X POST -H "Origin: $B" "$B/process_kill?pid=$PIDS"; echo
  for _ in $(seq 15); do
    curl -s -m10 "$B/processes_list" | grep -q "\"pid\":$PIDS," || break
    sleep 1
  done
  if curl -s -m10 "$B/processes_list" | grep -q "\"pid\":$PIDS,"; then echo "the old instance ($PIDS) does not stop"; exit 1; fi
fi

# Payload Manager checks the Origin header on every change.
curl -fsS -m60 -X POST -H "Origin: $B" -H 'Content-Type: application/octet-stream' \
  --data-binary @"$ELF" "$B/manage:upload?filename=$NAME&mode=replace"; echo " uploaded"
# Payload Manager picks the slot name itself (xpad-ps5.elf lands in local/xpad): read it back.
SLOT=$(curl -fsS -m10 "$B/list_payloads" | NAME="$NAME" python3 -c 'import json,os,sys; n=os.environ["NAME"]; print(next((i["slot"] for i in json.load(sys.stdin)["items"] if i["path"].endswith("/"+n)), ""))')
if [ -z "$SLOT" ]; then echo "uploaded, but Payload Manager does not list $NAME"; exit 1; fi
echo "slot: $SLOT"

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
