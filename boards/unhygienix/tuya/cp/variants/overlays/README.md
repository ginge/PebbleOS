# CP overlay fragments

The JSON recipes retain the pinned vendor hash, old context and replacement
count. Substantial additions use `new_file` paths relative to `variants/`.
Fragments mirror the target vendor path; the numeric suffix is the one-based
replacement position in that file's recipe.

Each `.inc` contains exact replacement bytes, including indentation and any
missing final newline. These are source fragments, not standalone C units.
Do not automatically format them: changing whitespace changes staged vendor
source. Review them beside their recipe's `old` context. Small substitutions
remain inline.

`load_overlay()` expands references before applying the existing hash and
replacement-count checks. The reviewed sleep-overlay pin covers the canonical
expanded JSON, preserving the original recipe digest. A fragment edit therefore
changes that digest and requires the same review as an inline recipe edit.
Build provenance separately hashes every physical recipe and fragment file.
