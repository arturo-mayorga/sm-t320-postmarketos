# Device-side configuration (mondrianwifi)

Copies of what lives on the tablet's rootfs, which pmbootstrap install does
not regenerate. Restore with scp after a fresh rootfs.

  hypr/hyprland.conf   -> ~user/.config/hypr/hyprland.conf
  waybar/config.jsonc  -> ~user/.config/waybar/config.jsonc (style.css is the
                          stock /etc/xdg/waybar/style.css)
  profile              -> ~user/.profile   starts the desktop on tty1

Autologin: /etc/inittab has
  tty1::respawn:/bin/login -f user
and /etc/local.d/xdg-runtime.start creates /run/user/10000 (no elogind/PAM).

Hyprland notes that matter on this hardware:
  monitor = DSI-1, preferred, 0x0, 2, transform, 3   (2560x1600 landscape, 2x)
  misc:vfr = true    with vfr=false Hyprland's fixed-rate rendering collides
                     with the pending page flip every other frame (EBUSY);
                     with vfr=true a busy terminal flips at a clean 60Hz.
  waybar: no pulseaudio/mpris/sway modules - pulseaudio module aborts without
          a pipewire-pulse server.

## System bits added for the radios (also on the tablet only)
  (the 90-bt-addr udev rule is gone: since r31 the DT's local-bd-address sets
   the Bluetooth address)
  /etc/conf.d/wpa_supplicant   wpa_supplicant_args="-u -s"   (dbus mode, for NetworkManager)
  /etc/ssh/sshd_config         AllowTcpForwarding yes        (pmOS default is no; needed for
                                                              the apk proxy tunnel)
  rc-update add: bluetooth wpa_supplicant networkmanager local
  packages added: bluez bluez-openrc bluez-btmgmt wpa_supplicant wpa_supplicant-openrc
                  networkmanager-wifi networkmanager-tui iw wvkbd

## Package installs without pmbootstrap sideload
The tablet has no internet. scratchpad proxy.py (a 40-line CONNECT proxy) on
the laptop, then
  ssh -R 3129:127.0.0.1:3128 tab 'sudo sh -c "export http_proxy=http://127.0.0.1:3129 https_proxy=$http_proxy; apk add ..."'
No sudo needed on the laptop.
  /etc/modules                 + uhid, hid-generic   (BLE HID keyboards/mice arrive
                                                      through bluez's HoG -> uhid; both
                                                      are modules and were not loaded)
  Paired: CorneKBH C9:2A:9C:08:8A:8A (BLE keyboard, trusted)
          MX Anywhere 2S E0:F5:F0:2D:AB:51 (BLE mouse, trusted)

## Keybindings (Omarchy's, ported)
  hypr/bindings.conf   sourced from hyprland.conf; same keys as the laptop's
                       `omarchy menu keybindings --print`, omarchy-* commands
                       replaced by bin/tab-* (-> ~user/.local/bin):
                         tab-clip           universal copy/paste/cut (terminal-aware)
                         tab-system-menu    Super+Esc: lock/suspend/logout/reboot/shutdown
                         tab-keybindings    Super+K: list bindings in fuzzel
                         tab-screenshot     Print (region) / Shift+Print (output)
                         tab-close-all      Ctrl+Alt+Del
                         tab-opacity-toggle Super+Backspace
  On-screen keyboard moved to Super+Alt+K (Super+K is Omarchy's keybindings menu).
  /etc/sudoers.d/tab-power  NOPASSWD reboot/poweroff/zzz for the system menu.
  Not ported (no tablet equivalent yet): browser/file manager, clipboard
  history, emoji picker, nightlight, idle toggle, bar panels, web-app keys.

## Neovim (LazyVim, same config as the laptop)
  nvim/nvim-config.tgz   ~/.config/nvim from the laptop minus the Omarchy
                         all-themes list; lazy-lock.json included so
                         `nvim --headless "+Lazy! restore" +qa` reproduces the
                         same plugin commits.
  nvim/tablet.lua        -> lua/plugins/tablet.lua: Mason ensure_installed = {}
                         (no armv7 binaries); LSPs from apk: clangd, gopls,
                         lua-language-server, yaml-language-server.
  Plugin fetch needs the apk proxy tunnel (git honours https_proxy).

## Tree-sitter parsers on the tablet
  LazyVim's startup auto-install is disabled in nvim/tablet.lua (it built ~40
  grammars in parallel and thrashed the tablet until sshd dropped and it
  rebooted). Install explicitly, throttled, with the clock set and the apk
  proxy tunnel up (git needs https):
    nvim --headless '+lua require("nvim-treesitter").install({"lua","c"}, { max_jobs = 1 }):wait(600000)' +qa
  Parsers live in ~/.local/share/nvim/site/parser.
  /etc/local.d/bluetooth-late.start   starts bluetoothd 20s after boot if it is
                                      not running (it was not, after a reboot).
  /etc/rc.conf rc_logger="YES"        boot log in /var/log/rc.log for the above.
  go (apk)                             LazyVim's Go extra runs `go env` on file open.

## Clock
The PM8941 RTC counts from battery insertion and Linux cannot write it (SPMI
arbiter: EPERM, even with allow-set-time). /etc/local.d/clock-offset.start
sets wall clock = RTC + /var/lib/clock-offset at boot; clock-offset.stop saves
the offset at shutdown. After setting the time by hand, run the .stop script
once so the offset is saved. Until Wi-Fi works there is no NTP.

## Browsers
  firefox (Super+Shift+Return / Super+Shift+B, private: Super+Shift+Alt+B)
  epiphany (Super+Shift+F) - light WebKitGTK browser for touch use

## Auto-rotate (accelerometer)
  DT: MPU-6515 on blsp2_i2c6 (from the vendor r12 dtsi), inv_mpu6050 module.
  iio-sensor-proxy (system service) + bin/tab-autorotate (exec-once) map
  orientation -> Hyprland transform for DSI-1 and the touch device.
  Super+Ctrl+R toggles it. Mapping in the script is calibrated by hand:
  the panel is natively portrait, landscape "right way up" is transform 3.
  /usr/local/bin/hyprland-dialog   bin/hyprland-dialog: stand-in for hyprland-guiutils
                                   (not packaged for armv7); stops the
                                   "hyprland-dialog missing from PATH" notice.
  /etc/polkit-1/rules.d/50-tablet-sensors.rules   lets user claim sensors via
        iio-sensor-proxy (no elogind session => polkit would deny)
  chrony (apk, default runlevel)   NTP over Wi-Fi; clock-offset.stop still saves
        the RTC offset for offline boots
  /etc/modprobe.d/wcn36xx.conf     options wcn36xx scan_offload=0 (r32 param)
  Wi-Fi: NetworkManager keyfile home.nmconnection (copied from the laptop)

## Firefox
  firefox/user.js -> ~/.config/mozilla/firefox/<profile>.default-release/user.js
  Software WebRender, no GPU process: on freedreno/a330 the GPU process fails
  EGL context creation (0x3009) and Firefox segfaults ~35s after start.
