/*
 * Original work Copyright (c) 2005-2008, The Android Open Source Project
 * Modified work Copyright (c) 2013, rovo89 and Tungstwenty
 * Modified work Copyright (c) 2015, Alibaba Mobile Infrastructure (Android) Team
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "dexposed.h"

#include <utils/Log.h>
#include <android_runtime/AndroidRuntime.h>
#include <stdio.h>
#include <sys/mman.h>
#include <cutils/properties.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <entrypoints/entrypoint_utils.h>
#include <entrypoints/interpreter/interpreter_entrypoints.h>

#include "quick_argument_visitor.h"
#include "ArgArray.h"

#define LOGLOG() LOG(INFO) << __FILE__ << ":" << __LINE__

namespace art {

    jclass dexposed_class = NULL;
    jmethodID dexposed_handle_hooked_method = NULL;

    bool dexposedOnVmCreated(JNIEnv *env, const char *) {

        dexposed_class = env->FindClass(DEXPOSED_CLASS);
        dexposed_class = reinterpret_cast<jclass>(env->NewGlobalRef(dexposed_class));

        if (dexposed_class == NULL) {
            LOG(ERROR) << "dexposed: Error while loading Dexposed class " << DEXPOSED_CLASS;
            env->ExceptionClear();
            return false;
        }

        LOG(INFO) << "dexposed: now initializing, Found Dexposed class " << DEXPOSED_CLASS;
        if (register_com_taobao_android_dexposed_DexposedBridge(env) != JNI_OK) {
            LOG(ERROR) << "dexposed: Could not register natives for " << DEXPOSED_CLASS;
            env->ExceptionClear();
            return false;
        }

        return true;
    }

    static jboolean initNative(JNIEnv* env, jclass) {

        LOG(INFO) << "dexposed: initNative";

        dexposed_handle_hooked_method =
                env->GetStaticMethodID(dexposed_class, "handleHookedMethod",
                                       "(Ljava/lang/reflect/Member;ILjava/lang/Object;Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;");
        if (dexposed_handle_hooked_method == NULL) {
            LOG(ERROR) << "dexposed: Could not find method " << DEXPOSED_CLASS << ".handleHookedMethod()";
            env->ExceptionClear();
            return false;
        }
        LOGLOG();
        return true;
    }


    extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *) {
        JNIEnv *env = NULL;
        jint result = -1;

        if (vm->GetEnv((void **) &env, JNI_VERSION_1_6) != JNI_OK) {
            return result;
        }

        int keepLoadingDexposed = dexposedOnVmCreated(env, NULL);
        LOG(INFO) << "JNI_OnLoad ---";
        if(keepLoadingDexposed)
            initNative(env, NULL);

        return JNI_VERSION_1_6;
    }

    extern "C" void art_quick_dexposed_invoke_handler();

    JValue InvokeXposedHandleHookedMethod(ScopedObjectAccessAlreadyRunnable &soa,
                                          const char *shorty,
                                          jobject rcvr_jobj, jmethodID method,
                                          std::vector<jvalue> &args)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {

        LOG(INFO) << "dexposed: InvokeXposedHandleHookedMethod";
        // Build argument array possibly triggering GC.
        soa.Self()->AssertThreadSuspensionIsAllowable();
        jobjectArray args_jobj = NULL;
        const JValue zero;
        int32_t target_sdk_version = Runtime::Current()->GetTargetSdkVersion();
        // Do not create empty arrays unless needed to maintain Dalvik bug compatibility.
        if (args.size() > 0 || (target_sdk_version > 0 && target_sdk_version <= 21)) {
            args_jobj = soa.Env()->NewObjectArray(args.size(), WellKnownClasses::java_lang_Object,
                                                  NULL);
            if (args_jobj == NULL) {
                CHECK(soa.Self()->IsExceptionPending());
                return zero;
            }
            for (size_t i = 0; i < args.size(); ++i) {
                if (shorty[i + 1] == 'L') {
                    jobject val = args.at(i).l;
                    soa.Env()->SetObjectArrayElement(args_jobj, i, val);
                } else {
                    JValue jv;
                    jv.SetJ(args.at(i).j);
                    mirror::Object *val = BoxPrimitive(Primitive::GetType(shorty[i + 1]), jv);
                    if (val == NULL) {
                        CHECK(soa.Self()->IsExceptionPending());
                        return zero;
                    }
                    soa.Decode<mirror::ObjectArray<mirror::Object> *>(args_jobj)->Set<false>(i,
                                                                                             val);
                }
            }
        }

#if PLATFORM_SDK_VERSION < 22
        const DexposedHookInfo *hookInfo =
                (DexposedHookInfo *) (soa.DecodeMethod(method)->GetNativeMethod());
#else
        const DexposedHookInfo *hookInfo =
                (DexposedHookInfo *) (soa.DecodeMethod(method)->GetEntryPointFromJni());
#endif


        // Call XposedBridge.handleHookedMethod(Member method, int originalMethodId, Object additionalInfoObj,
        //                                      Object thisObject, Object[] args)
        jvalue invocation_args[5];
        invocation_args[0].l = hookInfo->reflectedMethod;
        invocation_args[1].i = 0;
        invocation_args[2].l = hookInfo->additionalInfo;
        invocation_args[3].l = rcvr_jobj;
        invocation_args[4].l = args_jobj;
        jobject result =
                soa.Env()->CallStaticObjectMethodA(dexposed_class,
                                                   dexposed_handle_hooked_method,
                                                   invocation_args);

        // Unbox the result if necessary and return it.
        if (UNLIKELY(soa.Self()->IsExceptionPending())) {
            return zero;
        } else {
            if (shorty[0] == 'V' || (shorty[0] == 'L' && result == NULL)) {
                return zero;
            }
            StackHandleScope<1> hs(soa.Self());
            auto *proxy = soa.DecodeMethod(method)->GetInterfaceMethodIfProxy(sizeof(void*));
            // This can cause thread suspension.
            mirror::Object *rcvr = soa.Decode<mirror::Object *>(rcvr_jobj);
            mirror::Object *result_ref = soa.Decode<mirror::Object *>(result);
            mirror::Class *result_type = proxy->GetReturnType();
            JValue result_unboxed;
            if (!UnboxPrimitiveForResult(result_ref, result_type,
                                         &result_unboxed)) {
                DCHECK(soa.Self()->IsExceptionPending());
                return zero;
            }
            return result_unboxed;
        }
    }

    // Handler for invocation on proxy methods. On entry a frame will exist for the proxy object method
    // which is responsible for recording callee save registers. We explicitly place into jobjects the
    // incoming reference arguments (so they survive GC). We invoke the invocation handler, which is a
    // field within the proxy object, which will box the primitive arguments and deal with error cases.
    extern "C" uint64_t artQuickDexposedInvokeHandler(ArtMethod *proxy_method,
                                                      Object *receiver, Thread *self,
                                                      ArtMethod **sp)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {

        const bool is_static = proxy_method->IsStatic();

        LOG(INFO) << "dexposed: artQuickDexposedInvokeHandler is_static:" << is_static << " " <<
        PrettyMethod(proxy_method);

        // Ensure we don't get thread suspension until the object arguments are safely in jobjects.
        const char *old_cause = self->StartAssertNoThreadSuspension(
                "Adding to IRT proxy object arguments");

        // Register the top of the managed stack, making stack crawlable.
        DCHECK_EQ(*sp, proxy_method) << PrettyMethod(proxy_method);
        self->SetTopOfStack(sp);
//		DCHECK_EQ(proxy_method->GetFrameSizeInBytes(),
//				Runtime::Current()->GetCalleeSaveMethod(Runtime::kRefsAndArgs)->GetFrameSizeInBytes())
//				<< PrettyMethod(proxy_method);
        self->VerifyStack();
        // Start new JNI local reference state.
        JNIEnvExt *env = self->GetJniEnv();
        ScopedObjectAccessUnchecked soa(env);
        ScopedJniEnvLocalRefState env_state(env);
        // Create local ref. copies of proxy method and the receiver.
        jobject rcvr_jobj = is_static ? nullptr : soa.AddLocalReference<jobject>(receiver);

        std::vector<jvalue> args;
        uint32_t shorty_len = 0;
        const char *shorty = proxy_method->GetShorty(&shorty_len);
        LOG(INFO) << "dexposed: artQuickDexposedInvokeHandler shorty:" << shorty;

        BuildQuickArgumentVisitor local_ref_visitor(sp, is_static, shorty, shorty_len, &soa, &args);
        local_ref_visitor.VisitArguments();
        if (!is_static) {
            DCHECK_GT(args.size(), 0U) << PrettyMethod(proxy_method);
            args.erase(args.begin());
        }
        LOG(INFO) << "dexposed: artQuickDexposedInvokeHandler args.size:" << args.size();
        jmethodID proxy_methodid = soa.EncodeMethod(proxy_method);
        self->EndAssertNoThreadSuspension(old_cause);
        JValue result = InvokeXposedHandleHookedMethod(soa, shorty, rcvr_jobj, proxy_methodid,
                                                       args);
        local_ref_visitor.FixupReferences();
        return result.GetJ();
    }

    static void com_taobao_android_dexposed_DexposedBridge_hookMethodNative(
            JNIEnv *env, jclass, jobject java_method, jobject, jint,
            jobject additional_info) {
        LOGLOG();
        ScopedObjectAccess soa(env);
        LOGLOG();
        art::Thread *self = art::Thread::Current();

        LOGLOG();
        ArtMethod *art_method = ArtMethod::FromReflectedMethod(soa, java_method);

        LOG(INFO) << "dexposed: >>> hookMethodNative " << art_method << " " <<
        PrettyMethod(art_method);

        if (dexposedIsHooked(art_method)) {
            LOG(INFO) << "dexposed: >>> Already hooked " << art_method << " " <<
            PrettyMethod(art_method);
            return;
        }
        LOGLOG();

        // Create a backup of the ArtMethod object
        ArtMethod *backup_method = new ArtMethod(*art_method, sizeof(void*));
        LOGLOG();
        // Set private flag to avoid virtual table lookups during invocation
        backup_method->SetAccessFlags(
                backup_method->GetAccessFlags() /*| kAccXposedOriginalMethod*/);
        LOGLOG();
        // Create a Method/Constructor object for the backup ArtMethod object
        jobject reflect_method;
        if (art_method->IsConstructor()) {
            reflect_method = env->AllocObject(WellKnownClasses::java_lang_reflect_Constructor);
        } else {
            reflect_method = env->AllocObject(WellKnownClasses::java_lang_reflect_Method);
        }
        LOGLOG();
        env->SetLongField(reflect_method,
                            WellKnownClasses::java_lang_reflect_AbstractMethod_artMethod,
                            (jlong)backup_method);
        // Save extra information in a separate structure, stored instead of the native method
        DexposedHookInfo *hookInfo = reinterpret_cast<DexposedHookInfo *>(calloc(1,
                                                                                 sizeof(DexposedHookInfo)));
        LOGLOG();
        hookInfo->reflectedMethod = env->NewGlobalRef(reflect_method);
        hookInfo->additionalInfo = env->NewGlobalRef(additional_info);
        hookInfo->originalMethod = backup_method;

        LOGLOG();
#if PLATFORM_SDK_VERSION < 22
        art_method->SetNativeMethod(reinterpret_cast<uint8_t *>(hookInfo));
#else
        art_method->SetEntryPointFromJni(reinterpret_cast<void *>(hookInfo));
#endif

        LOGLOG();
        art_method->SetEntryPointFromQuickCompiledCode((void *) art_quick_dexposed_invoke_handler);

        art_method->SetAccessFlags((art_method->GetAccessFlags() & ~kAccNative));
        LOGLOG();
    }

    static bool dexposedIsHooked(ArtMethod *method) {
        LOGLOG();
        return (method->GetEntryPointFromQuickCompiledCode())
               == (void *) art_quick_dexposed_invoke_handler;
    }

    extern "C" jobject com_taobao_android_dexposed_DexposedBridge_invokeOriginalMethodNative(
            JNIEnv *env, jclass, jobject java_method, jint, jobject, jobject,
            jobject thiz, jobject args)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
        LOG(INFO) << "invokeOriginalMethodNative ---";

        ScopedObjectAccess soa(env);
        art::Thread *self = art::Thread::Current();

        ArtMethod *art_method = ArtMethod::FromReflectedMethod(soa, java_method);
        if (dexposedIsHooked(art_method)) {
            LOG(ERROR) << "dexposed: >>> invokeOriginalMethodNative dexposedIsHooked: " <<
            PrettyMethod(art_method);
            return nullptr;
        }
        Object *receiver = art_method->IsStatic() ? NULL : soa.Decode<Object *>(thiz);
        StackHandleScope<1> hs(soa.Self());
        auto *proxy = art_method->GetInterfaceMethodIfProxy(sizeof(void*));
        mirror::ObjectArray<mirror::Object> *objectArray = soa.Decode<mirror::ObjectArray<mirror::Object> *>(
                args);
        uint32_t arg_length = 0;
        const char *arg_shorty = proxy->GetShorty(&arg_length);
        ArgArray arg_array(arg_shorty, arg_length);
        arg_array.BuildArgArrayFromObjectArray(soa, receiver, objectArray, art_method);

        JValue result;
        art_method->Invoke(self, arg_array.GetArray(), arg_array.GetNumBytes(), &result,
                           arg_shorty);
        if (arg_shorty[0] == 'V')
            return nullptr;
        else if (arg_shorty[0] == 'L')
            return soa.AddLocalReference<jobject>(result.GetL());
        else
            return soa.AddLocalReference<jobject>(
                    BoxPrimitive(Primitive::GetType(arg_shorty[0]), result));
    }

    extern "C" jobject com_taobao_android_dexposed_DexposedBridge_invokeSuperNative(
            JNIEnv *env, jclass, jobject thiz, jobject args, jobject java_method, jobject, jobject,
            jint slot, jboolean check)

    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {

        LOG(INFO) << "dexposed: >>> invokeSuperNative";

        ScopedObjectAccess soa(env);
        art::Thread *self = art::Thread::Current();
        ArtMethod *method = ArtMethod::FromReflectedMethod(soa, java_method);

// Find the actual implementation of the virtual method.
        ArtMethod *art_method = method->FindOverriddenMethod(sizeof(void*));

        Object *receiver = art_method->IsStatic() ? NULL : soa.Decode<Object *>(thiz);
        StackHandleScope<1> hs(soa.Self());
        auto *proxy = art_method->GetInterfaceMethodIfProxy(sizeof(void*));
        mirror::ObjectArray<mirror::Object> *objectArray = soa.Decode<mirror::ObjectArray<mirror::Object> *>(
                args);
        uint32_t arg_length = 0;
        const char *arg_shorty = proxy->GetShorty(&arg_length);
        ArgArray arg_array(arg_shorty, arg_length);
        arg_array.BuildArgArrayFromObjectArray(soa, receiver, objectArray, art_method);

        JValue result;
        art_method->Invoke(self, arg_array.GetArray(), arg_array.GetNumBytes(), &result,
                           arg_shorty);
        if (arg_shorty[0] == 'V')
            return nullptr;
        else if (arg_shorty[0] == 'L')
            return soa.AddLocalReference<jobject>(result.GetL());
        else
            return soa.AddLocalReference<jobject>(
                    BoxPrimitive(Primitive::GetType(arg_shorty[0]), result));
    }

    static const JNINativeMethod dexposedMethods[] =
            {
                    {"hookMethodNative",  "(Ljava/lang/reflect/Member;Ljava/lang/Class;ILjava/lang/Object;)V",
                                                 (void *) com_taobao_android_dexposed_DexposedBridge_hookMethodNative},
                    {"invokeOriginalMethodNative",
                                          "(Ljava/lang/reflect/Member;I[Ljava/lang/Class;Ljava/lang/Class;Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;",
                                                 (void *) com_taobao_android_dexposed_DexposedBridge_invokeOriginalMethodNative},
                    {"invokeSuperNative", "(Ljava/lang/Object;[Ljava/lang/Object;Ljava/lang/reflect/Member;Ljava/lang/Class;[Ljava/lang/Class;Ljava/lang/Class;I)Ljava/lang/Object;",
                                                 (void *) com_taobao_android_dexposed_DexposedBridge_invokeSuperNative},
            };

    static int register_com_taobao_android_dexposed_DexposedBridge(JNIEnv *env) {
        LOG(INFO) << "registering native methods";
        #ifdef __arm__
        LOG(INFO) << "runing on arm cpu";
        #endif
        #ifdef __aarch64__
        LOG(INFO) << "runing on arm64 cpu";
        #endif
        #ifdef __i386__
        LOG(INFO) << "runing on x86 cpu";
        #endif
        #ifdef __x86_64__
        LOG(INFO) << "runing on x86_64 cpu";
        #endif
        return env->RegisterNatives(dexposed_class, dexposedMethods,
                                    sizeof(dexposedMethods) / sizeof(dexposedMethods[0]));
    }
}

