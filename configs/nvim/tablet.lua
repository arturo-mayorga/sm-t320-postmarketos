-- Tablet (armv7) overrides. Mason's prebuilt binaries are x86/aarch64 only, so
-- language servers come from apk (clangd, gopls, lua-language-server,
-- yaml-language-server) and are picked up from PATH by lspconfig.
return {
	{ "mason-org/mason.nvim", opts = { ensure_installed = {} } },
	{ "mason-org/mason-lspconfig.nvim", opts = { automatic_installation = false, ensure_installed = {} } },
}
