-- Tablet (armv7) overrides. Mason's prebuilt binaries are x86/aarch64 only, so
-- language servers come from apk (clangd, gopls, lua-language-server,
-- yaml-language-server). LazyVim only enables a server itself when it is
-- marked mason = false, so do that for the ones we have and turn off the rest.
local have = { lua_ls = true, clangd = true, gopls = true, yamlls = true }
return {
	{
		"mason-org/mason.nvim",
		opts = function(_, opts)
			opts.ensure_installed = {}
		end,
	},
	{
		"mason-org/mason-lspconfig.nvim",
		opts = function(_, opts)
			opts.ensure_installed = {}
			opts.automatic_installation = false
			opts.automatic_enable = false
		end,
	},
	{
		"neovim/nvim-lspconfig",
		opts = function(_, opts)
			for name, server in pairs(opts.servers or {}) do
				if type(server) == "table" then
					server.mason = false
					if not have[name] then
						server.enabled = false
					end
				end
			end
		end,
	},
	-- Parser builds: the plugin compiles every grammar in parallel (MAX_JOBS =
	-- 100), which on 4 cores and 2 GB thrashed the box until sshd dropped.
	{
		"nvim-treesitter/nvim-treesitter",
		opts = function()
			local ts = require("nvim-treesitter")
			if not ts._tablet_wrapped then
				local orig = ts.install
				ts.install = function(langs, o)
					o = o or {}
					o.max_jobs = 2
					return orig(langs, o)
				end
				ts._tablet_wrapped = true
			end
		end,
	},
	-- needs a yarn/npm build of its app; not worth it on this CPU
	{ "iamcco/markdown-preview.nvim", enabled = false },
}
