# toggle.nvim-style word toggling — Design

## Goal

Let users cycle a word under the cursor through a set of related values
(`true` → `false`, `on` → `off`, `public` → `protected` → `private`, ...) in
NORMAL mode, matching the behavior of
[toggle.nvim](https://github.com/leblocks/toggle.nvim) — but as a pure VimL
recipe in the user's own `svimrc`, not a new engine feature.

## Non-goals

- Not a new engine feature, not a new config file, not any change under
  `src/`. `svimrc` is already VimL sourced through libvim's real scripting
  engine (`:function`, `:let`, `expand()`, `<expr>` mappings all confirmed
  working by hand-testing this session) — the whole feature fits entirely in
  that existing layer.
- Not toggling single-character/symbol pairs (`(` ↔ `)`, `&&` ↔ `||`, etc.)
  — word groups only, per explicit scope decision.
- Not a user-facing config file for custom groups. Users add their own pairs
  by editing the `g:toggle_groups` VimL list directly in their own `svimrc`
  (it's already their file) — a second config format/parser would duplicate
  what a VimL list literal already does for free.
- Not visual-mode support (toggle.nvim supports toggling a visual selection)
  — out of scope, NORMAL-mode-under-cursor only, matching what was asked for.

## Key finding from spike testing (drives the whole design)

Hand-tested three ways of invoking a VimL function from a NORMAL-mode
mapping, live, in TextEdit through the running `univim` binary:

1. `nnoremap <C-t> :call ToggleTF()<CR>` — **works but glitches**: entering
   real command-line mode and leaving it within one keystroke's processing
   visibly confuses this app's own mode/cursor sync layer (`buffer_sync_mode`
   / `buffer_sync_cmdline` in `src/buffer.c`) — reproduced live: cursor shape
   changed, text could vanish, Escape stopped responding. This is a
   pre-existing bug in the AX/libvim mode-sync layer, out of scope to fix
   here, but load-bearing for *how* this feature must be built.
2. `nnoremap <C-t> <Cmd>call ToggleTF()<CR>` — **not supported at all**
   (silently does nothing). This libvim fork doesn't implement Vim 8.2's
   `<Cmd>` mapping.
3. `nnoremap <expr> <C-t> ToggleTFKeys()` (function *returns* a keystring,
   mapping re-feeds it as NORMAL-mode input, no command-line mode ever
   entered) — **works cleanly**, confirmed no glitch across repeated
   forward/backward toggles.

Conclusion: every function-driving mapping in this feature (and in the
general VimL-scripting README guidance) must use `<expr>` + a
keystring-returning function, never `:call ...<CR>`.

## Components

### `examples/svimrc_toggle` (new file)

Self-contained VimL snippet users copy into their own
`~/.config/univim/svimrc`:

```vim
let g:toggle_groups = [
  \ ['true', 'false'], ['True', 'False'], ['TRUE', 'FALSE'],
  \ ['on', 'off'], ['On', 'Off'], ['ON', 'OFF'],
  \ ... one row per case-style (lower/Title/UPPER) per pair ...
  \ ['public', 'protected', 'private'], ['Public', 'Protected', 'Private'], ['PUBLIC', 'PROTECTED', 'PRIVATE'],
  \ ['left', 'center', 'right'], ['Left', 'Center', 'Right'], ['LEFT', 'CENTER', 'RIGHT'],
  \ ]

function! ToggleWordKeys(reverse)
  let w = expand('<cword>')
  if empty(w) | return '' | endif

  " offset of cursor within the word (0-indexed), walked backward one
  " character at a time -- avoids building/escaping a regex out of `w`.
  let col0 = col('.') - 1
  let start = col0
  let line = getline('.')
  while start > 0 && line[start - 1] =~ '\w'
    let start -= 1
  endwhile
  let offset = col0 - start

  for grp in g:toggle_groups
    let idx = index(grp, w)
    if idx >= 0
      let next_idx = a:reverse
        \ ? (idx - 1 + len(grp)) % len(grp)
        \ : (idx + 1) % len(grp)
      let new_word = grp[next_idx]
      let new_offset = offset < len(new_word) ? offset : len(new_word) - 1
      return "ciw" . new_word . "\<Esc>b" . (new_offset > 0 ? new_offset . "l" : "")
    endif
  endfor

  return ''
endfunction

nnoremap <expr> <C-t>   ToggleWordKeys(0)
nnoremap <expr> <C-S-t> ToggleWordKeys(1)
```

Default `g:toggle_groups` content is a straight port of
[toggle.nvim's `defaults.lua`](https://github.com/leblocks/toggle.nvim/blob/master/lua/toggle/defaults.lua)
word groups (symbol pairs excluded — out of scope), one row per case-style:

- 2-way pairs (48): or/and, on/off, in/out, up/down, get/set, yes/no,
  min/max, asc/desc, top/bottom, all/none, add/remove, row/column,
  prev/next, head/tail, push/pop, send/receive, show/hide, open/close,
  read/write, lock/unlock, load/unload, allow/deny, todo/done, first/last,
  inner/outer, true/false, start/stop, begin/end, above/below, focus/blur,
  before/after, width/height, active/inactive, source/target,
  import/export, enable/disable, enabled/disabled, encode/decode,
  attach/detach, resolve/reject, encrypt/decrypt, visible/hidden,
  include/exclude, connect/disconnect, success/failure,
  subscribe/unsubscribe, serialize/deserialize, horizontal/vertical
- 3-way groups (2): public/protected/private, left/center/right

Each row appears three times (lower, Title, UPPER) — matching is exact
(`==#`-equivalent via `index()`), so case is preserved implicitly by which
row matched; no separate case-detection/reapplication logic needed.

### README addition

A new subsection near the existing "Text-shortcut expansion" section,
titled roughly "VimL scripting in svimrc" — general, not toggle-specific:

- States plainly that `svimrc` runs real VimL (`:function`, `:let`,
  `expand()`, control flow), not just `:map`/`:set` one-liners.
- Documents the spike-testing finding as a rule: **use `<expr>` mappings
  that return a keystring, never `nnoremap ... :call Func()<CR>`** — the
  latter visibly corrupts mode/cursor sync (cursor shape, vanishing text,
  dead Escape), confirmed by hand-testing.
- Points to `examples/svimrc_toggle` as a worked example of this pattern.

## Data flow

None inside the app — this is entirely libvim's existing VimL-eval and
key-remap machinery running inside the already-synced vim buffer. No changes
to `vn_engine`, `ax.c`, `event_tap.c`, or `buffer.c`. The AX sync layer picks
up the buffer mutation from `ciw<word><Esc>b<n>l` exactly like it does for
any other NORMAL-mode edit.

## Testing

No C code, so no C test. Verification is manual, in a real AX-visible app
(TextEdit, as used during this design's spike testing):

1. Type `true`, `Esc`, cursor on the word, `Ctrl-t` → becomes `false`;
   `Ctrl-t` again → back to `true`. Same for `Ctrl-S-t` (reverse direction)
   on a 3-way group (`public`/`protected`/`private`) to confirm wraparound
   both directions.
2. Cursor-offset preservation: put cursor on the *last* character of
   `private` (long word), toggle to `left` (short word) — cursor should
   land on `left`'s last character (clamped), not fall off the end.
3. Word not in any group (e.g. `hello`) + `Ctrl-t` → no-op, `hello`
   unchanged (confirms the empty-return passthrough path).
