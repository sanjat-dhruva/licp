# Deploying to the ZSOM-F01 {#deploy}

Build and verify first — see "Build for the ZSOM-F01" in the README.

```sh
cp build-zsom/licpd licpd-board
aarch64-none-linux-gnu-strip licpd-board
md5sum licpd-board
```

## Option A — SD card (no network)

The board's root filesystem is on the SD card (`/dev/mmcblk1p2` for SD boot;
`/dev/mmcblk0p2` for eMMC boot). Mounting that partition on the host writes
straight into the board's filesystem.

1. On the board: `sync && poweroff`, then power off and remove the card.
2. On the host, with the EXT4 partition mounted (e.g. `/media/<user>/ROOT`):

```sh
sudo cp licpd-board /media/<user>/ROOT/home/root/licpd
sudo chmod +x       /media/<user>/ROOT/home/root/licpd
sudo mkdir -p       /media/<user>/ROOT/etc/licp /media/<user>/ROOT/var/lib/licp
sudo cp config/licp.conf.example /media/<user>/ROOT/etc/licp/
sync && sudo umount /media/<user>/ROOT
```

3. Re-insert, boot, and check `md5sum /home/root/licpd` matches the host.

`sync` and a clean `umount` are required; pulling the card otherwise leaves the
filesystem dirty and forces an ext4 recovery on next boot.

## Option B — scp

With the board on the network (PetaLinux 2020.1 runs dropbear):

```sh
scp licpd-board config/licp.conf.example root@<board-ip>:/home/root/
```

## Bench run

```sh
cat > /etc/licp/licp-sim.conf <<'EOF'
store_path = /var/lib/licp/frames.log
device_table_path = /var/lib/licp/devices.bin
store_fsync_each = false
rx_backend = sim
tx_backend = sim
obc_tcp_port = 5600
EOF

mkdir -p /var/lib/licp
./licpd -c /etc/licp/licp-sim.conf --sim -v
```

Expect `licpd running (simulated radios)` and a `superframe <n>: 1
transmissions planned` line roughly every 45.5 seconds.