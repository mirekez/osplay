#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
base="${EXTREME_BUILD_ROOT:-$root/build/tests/extreme}"
stage="${EXTREME_ROOTFS_STAGE:-$base/rootfs}"
image="${EXTREME_ROOTFS_IMAGE:-$base/rootfs.ext4}"
mkdir -p "$stage"/{bin,sbin,usr/bin,usr/sbin,etc/init.d,etc/dropbear,root/.ssh,tmp,dev,proc,sys}
chmod 755 "$stage"
chmod 700 "$stage/root" "$stage/root/.ssh"
for app in busybox sh ash echo printf test cat ls mkdir pwd sleep true false ping ifconfig nc kill netstat top tty; do
  printf 'ECAPP\001busybox\n' > "$stage/bin/$app"
  chmod 755 "$stage/bin/$app"
done
printf 'ECAPP\001dropbear\n' > "$stage/usr/sbin/dropbear"
chmod 755 "$stage/usr/sbin/dropbear"
for name in passwd group profile shells; do
  cp "$root/tests/busybox/rootfs/etc/$name" "$stage/etc/$name"
done
cp "$root/tests/busybox/rootfs/root/.ssh/authorized_keys" "$stage/root/.ssh/authorized_keys"
if [[ -f "$base/test_key.pub" ]]; then printf '\n' >> "$stage/root/.ssh/authorized_keys"; cat "$base/test_key.pub" >> "$stage/root/.ssh/authorized_keys"; fi
chmod 600 "$stage/root/.ssh/authorized_keys"
base64 -d "$root/tests/busybox/rootfs/etc/dropbear/dropbear_ed25519_host_key.b64" > "$stage/etc/dropbear/dropbear_ed25519_host_key"
chmod 600 "$stage/etc/dropbear/dropbear_ed25519_host_key"
if [[ -n "${EXTREME_INIT_SCRIPT:-}" ]]; then
  cp "$EXTREME_INIT_SCRIPT" "$stage/etc/init.d/rcS"
else
  cat > "$stage/etc/init.d/rcS" <<'INIT'
#!/bin/sh
set -e
printf 'MIKOS:BUSYBOX_PRINTF %u %x\n' 42 305419896
test "$(echo extreme)" = extreme
printf 'MIKOS:BUSYBOX_OK\n'
exit 0
INIT
fi
chmod 755 "$stage/etc/init.d/rcS"
truncate -s 4M "$image"
/usr/sbin/mke2fs -q -F -t ext4 -b 1024 -I 128 -m 0 \
  -O extent,filetype,^64bit,^metadata_csum,^has_journal \
  -E lazy_itable_init=0 -d "$stage" "$image"

bash "$root/tests/busybox/set_rootfs_owners.sh" "$stage" "$image" /usr/sbin/debugfs
