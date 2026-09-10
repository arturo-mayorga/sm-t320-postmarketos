-- Tablet (armv7) overrides. Mason's prebuilt binaries are x86/aarch64 only, so
-- language servers come from apk (clangd, gopls, lua-language-server,
-- yaml-language-server) and are picked up from PATH by lspconfig.
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
	-- needs a yarn/npm build of its app; not worth it on this CPU
	{ "iamcco/markdown-preview.nvim", enabled = false },
}
