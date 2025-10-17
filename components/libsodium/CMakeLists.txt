# 注：这个路径需要根据实际项目结构调整
set(libsodium_srcs
    "libsodium/src/libsodium/crypto_generichash/blake2b/ref/blake2b-ref.c"
    "libsodium/src/libsodium/crypto_stream/salsa20/ref/salsa20_ref.c"
    "libsodium/src/libsodium/crypto_core/salsa/ref/core_salsa_ref.c"
    "libsodium/src/libsodium/randombytes/randombytes.c"
    "libsodium/src/libsodium/randombytes/internal/randombytes_internal_random.c"
    "libsodium/src/libsodium/randombytes/sysrandom/randombytes_sysrandom.c"
    "libsodium/src/libsodium/crypto_pwhash/argon2/argon2.c"
    "libsodium/src/libsodium/crypto_pwhash/argon2/argon2-core.c"
    "libsodium/src/libsodium/crypto_pwhash/argon2/argon2-encoding.c"
    "libsodium/src/libsodium/crypto_pwhash/argon2/argon2-fill-block-ref.c"
    "libsodium/src/libsodium/crypto_pwhash/argon2/blake2b-long.c"
    "libsodium/src/libsodium/crypto_pwhash/argon2/pwhash_argon2i.c"
    "libsodium/src/libsodium/crypto_pwhash/argon2/pwhash_argon2id.c"
    "libsodium/src/libsodium/crypto_aead/chacha20poly1305/sodium/aead_chacha20poly1305.c"
    "libsodium/src/libsodium/crypto_core/ed25519/core_ed25519.c"
    "libsodium/src/libsodium/crypto_core/ed25519/core_ristretto255.c"
    "libsodium/src/libsodium/crypto_core/ed25519/ref10/ed25519_ref10.c"
    "libsodium/src/libsodium/crypto_hash/crypto_hash.c"
    "libsodium/src/libsodium/crypto_sign/ed25519/sign_ed25519.c"
    "libsodium/src/libsodium/crypto_sign/ed25519/ref10/keypair.c"
    "libsodium/src/libsodium/crypto_sign/ed25519/ref10/open.c"
    "libsodium/src/libsodium/crypto_sign/ed25519/ref10/sign.c"
    "libsodium/src/libsodium/crypto_sign/ed25519/ref10/obsolete.c"
    "libsodium/src/libsodium/crypto_sign/crypto_sign.c"
    "libsodium/src/libsodium/crypto_hash/sha256/cp/hash_sha256_cp.c"
    "libsodium/src/libsodium/crypto_hash/sha256/hash_sha256.c"
    "libsodium/src/libsodium/crypto_hash/sha512/cp/hash_sha512_cp.c"
    "libsodium/src/libsodium/crypto_hash/sha512/hash_sha512.c"
    "libsodium/src/libsodium/crypto_onetimeauth/crypto_onetimeauth.c"
    "libsodium/src/libsodium/crypto_onetimeauth/poly1305/donna/poly1305_donna.c"
    "libsodium/src/libsodium/crypto_onetimeauth/poly1305/onetimeauth_poly1305.c"
    "libsodium/src/libsodium/crypto_scalarmult/crypto_scalarmult.c"
    "libsodium/src/libsodium/crypto_scalarmult/curve25519/ref10/x25519_ref10.c"
    "libsodium/src/libsodium/crypto_scalarmult/curve25519/scalarmult_curve25519.c"
    "libsodium/src/libsodium/crypto_stream/chacha20/ref/chacha20_ref.c"
    "libsodium/src/libsodium/crypto_stream/chacha20/stream_chacha20.c"
    "libsodium/src/libsodium/crypto_stream/crypto_stream.c"
    "libsodium/src/libsodium/crypto_verify/sodium/verify.c"
    "libsodium/src/libsodium/sodium/codecs.c"
    "libsodium/src/libsodium/sodium/core.c"
    "libsodium/src/libsodium/sodium/runtime.c"
    "libsodium/src/libsodium/sodium/utils.c"
    "port/version.c"
)

set(libsodium_include_dirs
    "libsodium/src/libsodium/include"
    "libsodium/src/libsodium/include/sodium"
    "port_include"
)

idf_component_register(
    SRCS ${libsodium_srcs}
    INCLUDE_DIRS ${libsodium_include_dirs}
    PRIV_REQUIRES
)

# 编译选项
target_compile_options(${COMPONENT_LIB} PRIVATE
    -D CONFIGURED=1
    -Wno-unused-function
    -Wno-unknown-pragmas
    -Wno-unused-variable
    -Wno-type-limits
)