-- Omarchy's current theme file (catppuccin), static on the tablet: on the
-- laptop lua/plugins/theme.lua is a symlink into ~/.config/omarchy/current.
return {
  {
    "catppuccin/nvim",
    name = "catppuccin",
    priority = 1000,
  },
  {
    "LazyVim/LazyVim",
    opts = {
      colorscheme = "catppuccin-nvim",
    },
  },
}
