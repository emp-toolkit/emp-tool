#ifndef EMP_ABI_GUARD_H__
#define EMP_ABI_GUARD_H__

// Link-time build-consistency guards. A build option that must agree across
// everything linked into one program gets a marker template with one symbol
// per setting. libemp-tool defines the symbol for the setting it was built
// with, and each translation unit that includes the option's header holds a
// guard that takes the address of the symbol for its own setting, so linking
// objects built with another setting fails on an undefined marker symbol.
//
// Nothing reads a guard, and an unused reference does not survive: the
// compiler drops unused definitions, and linker garbage collection
// (--gc-sections, -dead_strip) drops unreferenced sections, taking the
// reference and the link error with them. A guard is therefore an empty
// function that runs once at program start. Every linker keeps the list of
// startup functions, and the asm statement, which the compiler must keep,
// makes the function use the marker's address. Clang's -Wglobal-constructors
// reports each guard.
#define EMP_ABI_GUARD(name, sym)                \
    [[gnu::constructor]] static void name() {   \
        __asm__ volatile("" : : "r"(&(sym)));   \
    }                                           \
    static_assert(true)

#endif
