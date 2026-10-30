#!/usr/bin/env bash
set -eu

IP="${DISKOS_IP:?set DISKOS_IP to the device IP (shown in Debug Mode)}"
export SSHPASS="${DISKOS_PW:?set DISKOS_PW to the Debug Mode SSH password}"

usage() { echo "usage: $0 tap <x> <y> | swipe <x1> <y1> <x2> <y2>" >&2; exit 2; }

CMD="${1:-}"; shift || true
case "$CMD" in
  tap)   [ "$#" -eq 2 ] || usage ;;
  swipe) [ "$#" -eq 4 ] || usage ;;
  *)     usage ;;
esac
for a in "$@"; do
  case "$a" in ''|*[!0-9]*) echo "coordinates must be non-negative integers (0..359)" >&2; exit 2 ;; esac
done

SSH=(sshpass -e ssh -o StrictHostKeyChecking=accept-new -o ConnectTimeout=8)

if ! "${SSH[@]}" "root@$IP" 'test -x /usr/data/tinj'; then
  echo "tinj not found on device (/usr/data/tinj). Build it once and push it:" >&2
  echo "  docker run --rm -v \"\$PWD:/src\" -w /src diskos-ui-builder sh -c '\${CROSS}gcc -O2 -static -o tinj tinj.c'" >&2
  echo "  sshpass -e scp tinj root@\"\$DISKOS_IP\":/usr/data/tinj   # with SSHPASS=\$DISKOS_PW exported" >&2
  echo "  sshpass -e ssh root@\"\$DISKOS_IP\" chmod 755 /usr/data/tinj" >&2
  exit 1
fi

"${SSH[@]}" "root@$IP" "/usr/data/tinj $CMD $*"
