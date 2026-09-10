#!/bin/sh
# Build the mondrianwifi rootfs with a Hyprland desktop.
# Run from /home/amayorga/agents/tabpro84-port
set -e
cd "$(dirname "$0")"

PKGS="hyprland,xdg-desktop-portal-hyprland,xdg-desktop-portal,\
hyprlock,hypridle,hyprcursor,hyprpicker,\
waybar,fuzzel,mako,alacritty,swaybg,\
wl-clipboard,grim,slurp,\
pipewire,pipewire-pulse,wireplumber,\
seatd,dbus,polkit,\
mesa-dri-gallium,mesa-egl,mesa-gles,mesa-gbm,\
font-jetbrains-mono-nerd,font-dejavu,\
networkmanager,networkmanager-cli,openssh,\
wpa_supplicant,wpa_supplicant-openrc,bluez,bluez-openrc,bluez-btmgmt,wvkbd,\
bluetui,pulsemixer,networkmanager-wifi,networkmanager-tui,iw,\
neovim,ripgrep,fd,lazygit,gcc,musl-dev,make,curl,tree-sitter-cli,\
go,gopls,clang22-extra-tools,lua-language-server,yaml-language-server,nodejs,npm,\
firefox,epiphany,chrony,iio-sensor-proxy,\
htop,nano,git"

pmbootstrap shutdown
pmbootstrap -p "$PWD/pmaports" -w "$PWD/pmb-work" \
    install --password 147147 --add "$PKGS"
