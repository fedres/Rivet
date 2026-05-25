# vendor/

This directory holds vendored third-party libraries. Each is pinned to a specific
commit/tag and checked in directly — no network access required at build time.

| Library       | Version    | License    | Purpose                              |
|---------------|-----------|------------|--------------------------------------|
| sqlite/       | 3.45.0    | Public domain | Manifest & cache DB               |
| zstd/         | 1.5.5     | BSD        | Artifact compression                 |
| xxhash/       | 0.8.2     | BSD        | Fast checksums                       |
| mbedtls/      | 3.5.0     | Apache 2.0 | TLS for HTTPS downloads              |
| cpp-httplib/  | 0.15.3    | MIT        | HTTP client (built on mbedtls)       |
| simdjson/     | 3.8.0     | Apache 2.0 | High-speed JSON parsing              |
| tomlplusplus/ | 3.4.0     | MIT        | TOML manifest parsing                |

## Adding a dependency

1. Download the release archive.
2. Extract to `vendor/<name>/`.
3. Verify integrity: record the SHA256 of the tarball in `vendor/<name>/UPSTREAM`.
4. Add the subdirectory to the appropriate `CMakeLists.txt` target.
5. Update the table above.

## Updating a dependency

1. Replace the directory contents.
2. Update `vendor/<name>/UPSTREAM`.
3. Run `cmake --preset asan && ctest --preset asan` and confirm all tests pass.
