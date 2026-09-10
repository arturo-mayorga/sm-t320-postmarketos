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
  /etc/udev/rules.d/90-bt-addr.rules
      ACTION=="add", SUBSYSTEM=="bluetooth", KERNEL=="hci0", RUN+="/usr/bin/btmgmt -i hci0 public-addr 00:73:E0:26:9D:CC"
      (until the DT local-bd-address lands; btqcomsmd registers hci0 unconfigured)
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
