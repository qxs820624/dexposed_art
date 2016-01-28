#include "arch/x86_64/asm_support_x86_64.S"

// Offset of field Thread::tlsPtr_.card_table.
#define THREAD_CARD_TABLE_OFFSET 128
// Offset of field Thread::tlsPtr_.managed_stack.top_quick_frame_.
#define THREAD_TOP_QUICK_FRAME_OFFSET (THREAD_CARD_TABLE_OFFSET + (3 * __SIZEOF_POINTER__))
// For x86, the CFA is esp+4, the address above the pushed return address on the stack.

    /*
     * Macro that sets up the callee save frame to conform with
     * Runtime::CreateCalleeSaveMethod(kSaveAll)
     */
MACRO0(SETUP_SAVE_ALL_CALLEE_SAVE_FRAME)
#if defined(__APPLE__)
    int3
    int3
#else
    // R10 := Runtime::Current()
    movq _ZN3art7Runtime9instance_E@GOTPCREL(%rip), %r10
    movq (%r10), %r10
    // Save callee save registers to agree with core spills bitmap.
    PUSH r15  // Callee save.
    PUSH r14  // Callee save.
    PUSH r13  // Callee save.
    PUSH r12  // Callee save.
    PUSH rbp  // Callee save.
    PUSH rbx  // Callee save.
    // Create space for FPR args, plus space for ArtMethod*.
    subq MACRO_LITERAL(4 * 8 + 8), %rsp
    CFI_ADJUST_CFA_OFFSET(4 * 8 + 8)
    // Save FPRs.
    movq %xmm12, 8(%rsp)
    movq %xmm13, 16(%rsp)
    movq %xmm14, 24(%rsp)
    movq %xmm15, 32(%rsp)
    // R10 := ArtMethod* for save all callee save frame method.
    THIS_LOAD_REQUIRES_READ_BARRIER
    movq RUNTIME_SAVE_ALL_CALLEE_SAVE_FRAME_OFFSET(%r10), %r10
    // Store ArtMethod* to bottom of stack.
    movq %r10, 0(%rsp)
    // Store rsp as the top quick frame.
    movq %rsp, %gs:THREAD_TOP_QUICK_FRAME_OFFSET

    // Ugly compile-time check, but we only have the preprocessor.
    // Last +8: implicit return address pushed on stack when caller made call.
#if (FRAME_SIZE_SAVE_ALL_CALLEE_SAVE != 6 * 8 + 4 * 8 + 8 + 8)
#error "SAVE_ALL_CALLEE_SAVE_FRAME(X86_64) size not as expected."
#endif
#endif  // __APPLE__
END_MACRO

    /*
     * Macro that set calls through to artDeliverPendingExceptionFromCode, where the pending
     * exception is Thread::Current()->exception_.
     */
MACRO0(DELIVER_PENDING_EXCEPTION)
    SETUP_SAVE_ALL_CALLEE_SAVE_FRAME         // save callee saves for throw
    // (Thread*) setup
    movq %gs:THREAD_SELF_OFFSET, %rdi
    call SYMBOL(artDeliverPendingExceptionFromCode)  // artDeliverPendingExceptionFromCode(Thread*)
    UNREACHABLE
END_MACRO


MACRO0(RETURN_OR_DELIVER_PENDING_EXCEPTION)
    movq %gs:THREAD_EXCEPTION_OFFSET, %rcx // get exception field
    testq %rcx, %rcx               // rcx == 0 ?
    jnz 1f                         // if rcx != 0 goto 1
    ret                            // return
1:                                 // deliver exception on current thread
    DELIVER_PENDING_EXCEPTION
END_MACRO

MACRO0(RESTORE_REFS_AND_ARGS_CALLEE_SAVE_FRAME)
    // Restore FPRs.
    movq 16(%rsp), %xmm0
    movq 24(%rsp), %xmm1
    movq 32(%rsp), %xmm2
    movq 40(%rsp), %xmm3
    movq 48(%rsp), %xmm4
    movq 56(%rsp), %xmm5
    movq 64(%rsp), %xmm6
    movq 72(%rsp), %xmm7
    movq 80(%rsp), %xmm12
    movq 88(%rsp), %xmm13
    movq 96(%rsp), %xmm14
    movq 104(%rsp), %xmm15
    addq MACRO_LITERAL(80 + 4 * 8), %rsp
    CFI_ADJUST_CFA_OFFSET(-(80 + 4 * 8))
    // Restore callee and GPR args, mixed together to agree with core spills bitmap.
    POP rcx
    POP rdx
    POP rbx
    POP rbp
    POP rsi
    POP r8
    POP r9
    POP r12
    POP r13
    POP r14
    POP r15
END_MACRO

MACRO0(SETUP_REFS_AND_ARGS_CALLEE_SAVE_FRAME_WITH_METHOD_IN_RDI)
    // Save callee and GPR args, mixed together to agree with core spills bitmap.
    PUSH r15  // Callee save.
    PUSH r14  // Callee save.
    PUSH r13  // Callee save.
    PUSH r12  // Callee save.
    PUSH r9   // Quick arg 5.
    PUSH r8   // Quick arg 4.
    PUSH rsi  // Quick arg 1.
    PUSH rbp  // Callee save.
    PUSH rbx  // Callee save.
    PUSH rdx  // Quick arg 2.
    PUSH rcx  // Quick arg 3.
    // Create space for FPR args and create 2 slots for ArtMethod*.
    subq LITERAL(80 + 4 * 8), %rsp
    CFI_ADJUST_CFA_OFFSET(80 + 4 * 8)
    // Save FPRs.
    movq %xmm0, 16(%rsp)
    movq %xmm1, 24(%rsp)
    movq %xmm2, 32(%rsp)
    movq %xmm3, 40(%rsp)
    movq %xmm4, 48(%rsp)
    movq %xmm5, 56(%rsp)
    movq %xmm6, 64(%rsp)
    movq %xmm7, 72(%rsp)
    movq %xmm12, 80(%rsp)
    movq %xmm13, 88(%rsp)
    movq %xmm14, 96(%rsp)
    movq %xmm15, 104(%rsp)
    // Store ArtMethod to bottom of stack.
    movq %rdi, 0(%rsp)
    // Store rsp as the stop quick frame.
    movq %rsp, %gs:THREAD_TOP_QUICK_FRAME_OFFSET
END_MACRO


    .extern artQuickDexposedInvokeHandler
DEFINE_FUNCTION art_quick_dexposed_invoke_handler
    SETUP_REFS_AND_ARGS_CALLEE_SAVE_FRAME_WITH_METHOD_IN_RDI

    movq %gs:THREAD_SELF_OFFSET, %rdx       // Pass Thread::Current().
    movq %rsp, %rcx                         // Pass SP.
    call SYMBOL(artQuickDexposedInvokeHandler) // (proxy method, receiver, Thread*, SP)
    RESTORE_REFS_AND_ARGS_CALLEE_SAVE_FRAME
    movq %rax, %xmm0                        // Copy return value in case of float returns.
    RETURN_OR_DELIVER_PENDING_EXCEPTION
END_FUNCTION art_quick_dexposed_invoke_handler
