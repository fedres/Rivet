// rivet landing — OS detection + terminal typewriter + copy button

(() => {
    const BASE = "https://github.com/fedres/Rivet/releases/latest/download";

    // os → { label, command (with rich span markup), plain (for clipboard), prereq }
    const COMMANDS = {
        linux: {
            label: "linux",
            command: `<span class="prompt">$</span> curl -fsSL <span class="url">${BASE}/install.sh</span> | sh`,
            plain:  `curl -fsSL ${BASE}/install.sh | sh`,
            prereq: "no platform SDK needed — glibc + kernel headers ship with every distro.",
        },
        macos: {
            label: "macos",
            command: `<span class="prompt">$</span> curl -fsSL <span class="url">${BASE}/install.sh</span> | sh`,
            plain:  `curl -fsSL ${BASE}/install.sh | sh`,
            prereq: "<strong>no rivet-specific prereq</strong> &mdash; macOS expects the standard Xcode CLI tools that <em>every</em> C/C++ toolchain on macOS needs (cargo, brew, gcc, anything). If you've ever run <code>cc</code>, you have them. If not: <code>xcode-select --install</code> (one-time, ~1&nbsp;GB).",
        },
        windows: {
            label: "windows",
            command: `<span class="prompt">PS&gt;</span> irm <span class="url">${BASE}/install.ps1</span> | iex`,
            plain:  `irm ${BASE}/install.ps1 | iex`,
            prereq: "<strong>no prereq</strong> &mdash; rivet ships llvm-mingw, so the Windows SDK / VS Build Tools install you'd normally need is already inside the bundle.",
        },
    };

    const cmdEl     = document.getElementById("install-cmd");
    const osLabel   = document.getElementById("os-label");
    const prereqEl  = document.getElementById("prereq");
    const copyBtn   = document.getElementById("copy-btn");
    const tabs      = Array.from(document.querySelectorAll(".os-tab"));

    let currentOs = "linux";
    let typewriterToken = 0;

    // ─── OS detection ───────────────────────────────────────────────
    function detectOS() {
        const ua = (navigator.userAgent || "").toLowerCase();
        const platform = ((navigator.userAgentData && navigator.userAgentData.platform) ||
                          navigator.platform || "").toLowerCase();
        if (platform.includes("win") || ua.includes("windows")) return "windows";
        if (platform.includes("mac") || ua.includes("mac os") || ua.includes("macos")) return "macos";
        return "linux";  // also covers BSDs / unknowns — install.sh works.
    }

    // ─── typewriter ─────────────────────────────────────────────────
    // Renders the command character-by-character. We feed it HTML and
    // expand inside spans so the syntax highlighting comes out cleanly.
    function typeInto(el, html) {
        const myToken = ++typewriterToken;

        // Strip tags to get the typing sequence, then build a parallel
        // map of when to reopen spans.
        // Simpler: render the full html first, then animate width via CSS.
        // But CSS approach has issues with wrapping. Let's just write chars
        // through innerHTML, accepting one-shot tag boundaries.
        el.innerHTML = "";
        const tokens = tokenizeHtml(html);
        let i = 0;
        let buf = "";

        function step() {
            if (myToken !== typewriterToken) return;
            if (i >= tokens.length) return;
            const t = tokens[i++];
            if (t.tag) {
                buf += t.html;     // open + close tags emitted together for chunks
                el.innerHTML = buf;
                step();
            } else {
                // type each char in the text token
                for (const ch of t.html) {
                    buf += ch;
                    el.innerHTML = buf;
                }
                setTimeout(step, 18);
            }
        }
        step();
    }

    // Parse an HTML string into ordered tokens. Spans get emitted as
    // (open-tag + text + close-tag) chunks — we type the text inside but
    // the wrapping is instant per chunk. Plain text becomes a typing
    // token. This avoids the "type letters into broken markup" problem.
    function tokenizeHtml(html) {
        const out = [];
        const re = /<span class="([^"]+)">([^<]*)<\/span>/g;
        let last = 0, m;
        while ((m = re.exec(html)) !== null) {
            if (m.index > last)
                out.push({ tag: false, html: html.substring(last, m.index) });
            // Emit the wrapper open as a tag-only token,
            // then the inner text as a typing token,
            // then the close as a tag-only token.
            out.push({ tag: true, html: `<span class="${m[1]}">` });
            out.push({ tag: false, html: m[2] });
            out.push({ tag: true, html: "</span>" });
            last = re.lastIndex;
        }
        if (last < html.length)
            out.push({ tag: false, html: html.substring(last) });
        return out;
    }

    // ─── UI ─────────────────────────────────────────────────────────
    function selectOS(os, { animate = true } = {}) {
        if (!COMMANDS[os]) os = "linux";
        currentOs = os;
        const c = COMMANDS[os];
        osLabel.textContent = c.label;
        prereqEl.innerHTML  = c.prereq;
        if (animate) {
            typeInto(cmdEl, c.command);
        } else {
            cmdEl.innerHTML = c.command;
        }
        tabs.forEach(t => {
            t.setAttribute("aria-selected", t.dataset.os === os ? "true" : "false");
        });
    }

    tabs.forEach(t => {
        t.addEventListener("click", () => selectOS(t.dataset.os));
    });

    copyBtn.addEventListener("click", async () => {
        const text = COMMANDS[currentOs].plain;
        try {
            await navigator.clipboard.writeText(text);
            copyBtn.textContent = "copied ✓";
            copyBtn.classList.add("copied");
            setTimeout(() => {
                copyBtn.textContent = "copy";
                copyBtn.classList.remove("copied");
            }, 1400);
        } catch {
            // older browsers: fall back to selection
            const r = document.createRange();
            r.selectNodeContents(cmdEl);
            const sel = window.getSelection();
            sel.removeAllRanges();
            sel.addRange(r);
        }
    });

    // boot
    selectOS(detectOS());
})();
