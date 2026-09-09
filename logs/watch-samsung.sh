#!/usr/bin/env bash
# Watch for a Samsung device (vendor 04e8) appearing on USB.
echo "Watching for Samsung (04e8) — replug the tablet now. Ctrl-C to stop."
snapshot() {
  for d in /sys/bus/usb/devices/*/; do
    [ -f "$d/idVendor" ] || continue
    printf "%s %s:%s %s %s\n" "$(basename $d)" "$(cat $d/idVendor)" \
      "$(cat $d/idProduct)" "$(cat $d/manufacturer 2>/dev/null)" \
      "$(cat $d/product 2>/dev/null)"
  done
}
prev=$(snapshot)
while true; do
  cur=$(snapshot)
  if [ "$cur" != "$prev" ]; then
    diff <(echo "$prev") <(echo "$cur") | grep '^>' | sed 's/^> /  NEW: /'
    echo "$cur" | grep -i '04e8' && echo "  *** SAMSUNG DETECTED ***"
    prev=$cur
  fi
  sleep 1
done
