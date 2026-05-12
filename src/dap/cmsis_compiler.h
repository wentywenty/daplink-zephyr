/*
 * CMSIS Compiler Compatibility Stub for Zephyr
 *
 * Provides ARM CMSIS compiler abstraction macros using GCC syntax.
 */

#ifndef CMSIS_COMPILER_H_
#define CMSIS_COMPILER_H_

#include <stdint.h>
#include <stddef.h>
#include <zephyr/arch/arch_interface.h>

#ifndef __STATIC_FORCEINLINE
  #define __STATIC_FORCEINLINE static inline __attribute__((always_inline))
#endif
#ifndef __STATIC_INLINE
  #define __STATIC_INLINE  static inline
#endif
#ifndef __WEAK
  #define __WEAK          __attribute__((weak))
#endif
#ifndef __ALIGNED
  #define __ALIGNED(x)    __attribute__((aligned(x)))
#endif
#ifndef __PACKED
  #define __PACKED        __attribute__((packed))
#endif
#ifndef __PACKED_STRUCT
  #define __PACKED_STRUCT struct __attribute__((packed))
#endif

/* Unaligned access helpers */
__PACKED_STRUCT T_UINT16 { uint16_t v; };
__PACKED_STRUCT T_UINT32 { uint32_t v; };

#ifndef __UNALIGNED_UINT16_READ
  #define __UNALIGNED_UINT16_READ(addr) (((struct T_UINT16 *)(void *)(addr))->v)
#endif
#ifndef __UNALIGNED_UINT16_WRITE
  __PACKED_STRUCT T_UINT16_W { uint16_t v; };
  #define __UNALIGNED_UINT16_WRITE(addr, val) (void)((((struct T_UINT16_W *)(void *)(addr))->v) = (val))
#endif
#ifndef __UNALIGNED_UINT32_READ
  #define __UNALIGNED_UINT32_READ(addr) (((struct T_UINT32 *)(void *)(addr))->v)
#endif
#ifndef __UNALIGNED_UINT32_WRITE
  __PACKED_STRUCT T_UINT32_W { uint32_t v; };
  #define __UNALIGNED_UINT32_WRITE(addr, val) (void)((((struct T_UINT32_W *)(void *)(addr))->v) = (val))
#endif

#ifndef __NOP
#define __NOP() arch_nop()
#endif

/* Boolean type compatibility */
#ifndef BOOL
  typedef uint32_t BOOL;
#endif
#ifndef __TRUE
  #define __TRUE  1
#endif
#ifndef __FALSE
  #define __FALSE 0
#endif

#endif /* CMSIS_COMPILER_H_ */
