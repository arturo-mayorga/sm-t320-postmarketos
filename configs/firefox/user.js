// Adreno 330 / freedreno: EGL context creation fails for the GPU process and
// Firefox segfaults; use software WebRender and keep everything off the GPU.
user_pref("gfx.webrender.software", true);
user_pref("layers.acceleration.disabled", true);
user_pref("layers.gpu-process.enabled", false);
user_pref("media.hardware-video-decoding.enabled", false);
user_pref("webgl.disabled", true);
user_pref("gfx.canvas.accelerated", false);
user_pref("dom.ipc.processCount", 2);
user_pref("browser.sessionstore.resume_from_crash", false);
