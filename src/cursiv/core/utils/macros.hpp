#pragma once
// Every compiler builtin, attribute and intrinsic the tree uses is named here
// and nowhere else, and each but CURSIV_MUSTTAIL has a fallback that compiles.
// The grep that keeps it true -- no `__builtin_`, no `[[gnu::`, no `_mm_`
// outside this file.

#if defined(__has_builtin)
#define CURSIV_HAS_BUILTIN(x) __has_builtin(x)
#else
#define CURSIV_HAS_BUILTIN(x) 0
#endif

// A whole pack deduplicated in one expansion, first occurrence winning and the
// order kept, at zero instantiations. Where the compiler has none, typelist
// folds the pack itself; `-DCURSIV_HAS_DEDUP_PACK=0` makes that fold a real
// build.
#ifndef CURSIV_HAS_DEDUP_PACK
#if CURSIV_HAS_BUILTIN(__builtin_dedup_pack)
#define CURSIV_HAS_DEDUP_PACK 1
#else
#define CURSIV_HAS_DEDUP_PACK 0
#endif
#endif
#if CURSIV_HAS_DEDUP_PACK && !defined(CURSIV_DEDUP_PACK)
#define CURSIV_DEDUP_PACK(...) __builtin_dedup_pack<__VA_ARGS__>
#endif

// The Nth type of a pack, without a class per query.
#define CURSIV_PACK_ELEMENT(I, ...) __type_pack_element<I, __VA_ARGS__>

// Hints, never semantics: an empty fallback costs code size and changes no
// answer.

#if defined(__GNUC__) || defined(__clang__)
#define CURSIV_COLD [[gnu::cold]]
#else
#define CURSIV_COLD
#endif

#if defined(__GNUC__) || defined(__clang__)
#define CURSIV_NOINLINE [[gnu::noinline]]
#elif defined(_MSC_VER)
#define CURSIV_NOINLINE __declspec(noinline)
#else
#define CURSIV_NOINLINE
#endif

#if defined(__GNUC__) || defined(__clang__)
#define CURSIV_ALWAYS_INLINE __attribute__((always_inline))
#elif defined(_MSC_VER)
#define CURSIV_ALWAYS_INLINE __forceinline
#else
#define CURSIV_ALWAYS_INLINE
#endif

// A callee that saves no register for its caller: a call into it costs the
// caller its own live registers, not the callee's whole set.
#if defined(__x86_64__) && defined(__has_attribute)
#if __has_attribute(preserve_none)
#define CURSIV_PRESERVE_NONE __attribute__((preserve_none))
#endif
#endif
#ifndef CURSIV_PRESERVE_NONE
#define CURSIV_PRESERVE_NONE
#endif

// A call the compiler makes a jump or refuses: left undefined where it is not
// guaranteed, since a plain call grows the stack at every use. Clang for wasm
// without `-mtail-call` drops the attribute with a mere warning.
#if defined(__wasm__) && !defined(__wasm_tail_call__)
#elif defined(__clang__)
#if __has_cpp_attribute(clang::musttail)
#define CURSIV_MUSTTAIL [[clang::musttail]]
#endif
#elif defined(__GNUC__)
#if __has_cpp_attribute(gnu::musttail)
#define CURSIV_MUSTTAIL [[gnu::musttail]]
#endif
#endif
