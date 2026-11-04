#if GADGET_OS_NATIVE_APP_LOADER
#include <stdint.h>
// Deliberately no host symbol exports. Apps receive a versioned function table.
uintptr_t elf_find_sym_default(const char *name) { (void)name; return 0; }
#endif
