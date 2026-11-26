/* Force-included (-include) by native aarch64 cross builds made on a newer Ubuntu (glibc 2.43 headers), so the .so
 * loads on MPC OS 3.10.1 (glibc 2.39; the Docker builds use Debian bookworm, 2.36, and don't need this).
 *
 * With _GNU_SOURCE, glibc >= 2.38 headers send strtol/strtoul/sscanf... to their C23 variants (__isoc23_*, GLIBC_2.38),
 * and glibc 2.43 gave log10f a new symbol version. Bind those references to the versions every aarch64 glibc has
 * (2.17). Differences: the C23 variants also accept "0b" binary prefixes and %b, which nothing here uses.
 * Check after linking: readelf -V <so> | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1   must be <= 2.39. */
#if defined(__aarch64__)
__asm__(".symver __isoc23_strtol,strtol@GLIBC_2.17");
__asm__(".symver __isoc23_strtoul,strtoul@GLIBC_2.17");
__asm__(".symver __isoc23_strtoll,strtoll@GLIBC_2.17");
__asm__(".symver __isoc23_strtoull,strtoull@GLIBC_2.17");
__asm__(".symver __isoc23_sscanf,__isoc99_sscanf@GLIBC_2.17");
__asm__(".symver __isoc23_vsscanf,__isoc99_vsscanf@GLIBC_2.17");
__asm__(".symver __isoc23_fscanf,__isoc99_fscanf@GLIBC_2.17");
__asm__(".symver __isoc23_scanf,__isoc99_scanf@GLIBC_2.17");
__asm__(".symver log10f,log10f@GLIBC_2.17");
#endif
