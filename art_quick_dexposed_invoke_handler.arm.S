#include "arch/arm/asm_support_arm.S"
; #include "read_barrier_c.h"

    /*
     * Macro that sets up the callee save frame to conform with
     * Runtime::CreateCalleeSaveMethod(kSaveAll)
     */
.macro SETUP_SAVE_ALL_CALLEE_SAVE_FRAME rTemp1, rTemp2
    push {r4-r11, lr}                             @ 9 words (36 bytes) of callee saves.
    .cfi_adjust_cfa_offset 36
    .cfi_rel_offset r4, 0
    .cfi_rel_offset r5, 4
    .cfi_rel_offset r6, 8
    .cfi_rel_offset r7, 12
    .cfi_rel_offset r8, 16
    .cfi_rel_offset r9, 20
    .cfi_rel_offset r10, 24
    .cfi_rel_offset r11, 28
    .cfi_rel_offset lr, 32
    vpush {s16-s31}                               @ 16 words (64 bytes) of floats.
    .cfi_adjust_cfa_offset 64
    sub sp, #12                                   @ 3 words of space, bottom word will hold Method*
    .cfi_adjust_cfa_offset 12
    RUNTIME_CURRENT1 \rTemp1, \rTemp2             @ Load Runtime::Current into rTemp1.
    THIS_LOAD_REQUIRES_READ_BARRIER
    ldr \rTemp1, [\rTemp1, #RUNTIME_SAVE_ALL_CALLEE_SAVE_FRAME_OFFSET] @ rTemp1 is kSaveAll Method*.
    str \rTemp1, [sp, #0]                         @ Place Method* at bottom of stack.
    str sp, [r9, #THREAD_TOP_QUICK_FRAME_OFFSET]  @ Place sp in Thread::Current()->top_quick_frame.

     // Ugly compile-time check, but we only have the preprocessor.
#if (FRAME_SIZE_SAVE_ALL_CALLEE_SAVE != 36 + 64 + 12)
#error "SAVE_ALL_CALLEE_SAVE_FRAME(ARM) size not as expected."
#endif
.endm

    /*
     * Macro that set calls through to artDeliverPendingExceptionFromCode, where the pending
     * exception is Thread::Current()->exception_
     */
.macro DELIVER_PENDING_EXCEPTION
    .fnend
    .fnstart
    SETUP_SAVE_ALL_CALLEE_SAVE_FRAME r0, r1    @ save callee saves for throw
    mov    r0, r9                              @ pass Thread::Current
    b      artDeliverPendingExceptionFromCode  @ artDeliverPendingExceptionFromCode(Thread*)
.endm


.macro RESTORE_REFS_ONLY_CALLEE_SAVE_FRAME
    add sp, #4               @ bottom word holds Method*
    .cfi_adjust_cfa_offset -4
    pop {r5-r8, r10-r11, lr} @ 7 words of callee saves
    .cfi_restore r5
    .cfi_restore r6
    .cfi_restore r7
    .cfi_restore r8
    .cfi_restore r10
    .cfi_restore r11
    .cfi_restore lr
    .cfi_adjust_cfa_offset -28
.endm

    /*
     * Macro that sets up the callee save frame to conform with
     * Runtime::CreateCalleeSaveMethod(kRefsAndArgs).
     */
.macro SETUP_REFS_AND_ARGS_CALLEE_SAVE_FRAME_REGISTERS_ONLY
    push {r1-r3, r5-r8, r10-r11, lr}   @ 10 words of callee saves and args.
    .cfi_adjust_cfa_offset 40
    .cfi_rel_offset r1, 0
    .cfi_rel_offset r2, 4
    .cfi_rel_offset r3, 8
    .cfi_rel_offset r5, 12
    .cfi_rel_offset r6, 16
    .cfi_rel_offset r7, 20
    .cfi_rel_offset r8, 24
    .cfi_rel_offset r10, 28
    .cfi_rel_offset r11, 32
    .cfi_rel_offset lr, 36
    vpush {s0-s15}                     @ 16 words of float args.
    .cfi_adjust_cfa_offset 64
    sub sp, #8                         @ 2 words of space, bottom word will hold Method*
    .cfi_adjust_cfa_offset 8
    // Ugly compile-time check, but we only have the preprocessor.
#if (FRAME_SIZE_REFS_AND_ARGS_CALLEE_SAVE != 40 + 64 + 8)
#error "REFS_AND_ARGS_CALLEE_SAVE_FRAME(ARM) size not as expected."
#endif
.endm

.macro SETUP_REFS_AND_ARGS_CALLEE_SAVE_FRAME_WITH_METHOD_IN_R0
    SETUP_REFS_AND_ARGS_CALLEE_SAVE_FRAME_REGISTERS_ONLY
    str r0, [sp, #0]                   @ Store ArtMethod* to bottom of stack.
    str sp, [r9, #THREAD_TOP_QUICK_FRAME_OFFSET]  @ Place sp in Thread::Current()->top_quick_frame.
.endm


    /*
     * Called by managed code that is attempting to call a method on a proxy class. On entry
     * r0 holds the proxy method and r1 holds the receiver; r2 and r3 may contain arguments. The
     * frame size of the invoked proxy method agrees with a ref and args callee save frame.
     */
     .extern artQuickDexposedInvokeHandler
ENTRY art_quick_dexposed_invoke_handler
    SETUP_REFS_AND_ARGS_CALLEE_SAVE_FRAME_WITH_METHOD_IN_R0
    mov     r2, r9                 @ pass Thread::Current
    mov     r3, sp                 @ pass SP
    blx     artQuickDexposedInvokeHandler  @ (Method* proxy method, receiver, Thread*, SP)
    ldr     r2, [r9, #THREAD_EXCEPTION_OFFSET]  @ load Thread::Current()->exception_
    // Tear down the callee-save frame. Skip arg registers.
    add     sp, #(FRAME_SIZE_REFS_AND_ARGS_CALLEE_SAVE - FRAME_SIZE_REFS_ONLY_CALLEE_SAVE)
    .cfi_adjust_cfa_offset -(FRAME_SIZE_REFS_AND_ARGS_CALLEE_SAVE - FRAME_SIZE_REFS_ONLY_CALLEE_SAVE)
    RESTORE_REFS_ONLY_CALLEE_SAVE_FRAME
    cbnz    r2, 1f                 @ success if no exception is pending
    vmov    d0, r0, r1             @ store into fpr, for when it's a fpr return...
    bx      lr                     @ return on success
1:
    DELIVER_PENDING_EXCEPTION
END art_quick_dexposed_invoke_handler
