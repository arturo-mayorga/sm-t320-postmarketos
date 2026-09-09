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
