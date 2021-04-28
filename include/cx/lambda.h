#pragma once

#include <cx/idioms.h>
#include <cx/templates.h>

//Conditional stl dependencies if CX was built with stl support enabled
#ifdef CX_STL_SUPPORT
 #include <memory>
 #include <exception>

 #define CX_STL_SUPPORT_EXPR(expr) expr
#else
 #pragma message \
  "Building without STL support; 'CX::AllocLambda<...>' will be unavailable."

 #define CX_STL_SUPPORT_EXPR(expr)
#endif

#if defined(CX_LAMBDA_BUF_ALIGN) || defined(CX_LAMBDA_BUF_SIZE)
 #pragma message \
  "Using user-specified buffer size and alignment for non-allocating "\
  "polymorphic function wrapper implementation."

 //Check that both 'CX_LAMBDA_BUF_ALIGN' and 'CX_LAMBDA_BUF_SIZE'
 //were specified together
 #ifndef CX_LAMBDA_BUF_ALIGN
  #error \
   "You must specify 'CX_LAMBDA_BUF_ALIGN' when overriding "\
   "'CX_LAMBDA_BUF_SIZE'"
 #endif

 #ifdef CX_LAMBDA_BUF_SIZE
  #error \
   "You must specify 'CX_LAMBDA_BUF_SIZE' when overriding "\
   "'CX_LAMBDA_BUF_ALIGN'"
 #endif

 //Check that the user-specified values are valid
 #if CX_LAMBDA_BUF_ALIGN < CX_LAMBDA_BUF_SIZE
  #error \
   "'CX_LAMBDA_BUF_ALIGN' must be greater than or equal to "\
   "'CX_LAMBDA_BUF_SIZE'"
 #endif
#else
 #define CX_LAMBDA_BUF_ALIGN (sizeof(void *) * 8)
 #define CX_LAMBDA_BUF_SIZE (sizeof(void *) * 8)
#endif

namespace CX {
 //Supporting exceptions
 struct UninitializedLambdaError CX_STL_SUPPORT_EXPR(: std::exception) {
  char const * what() const noexcept CX_STL_SUPPORT_EXPR(override) {
   return "Lambda is uninitialized";
  }
 };

 struct IncompatibleLambdaError CX_STL_SUPPORT_EXPR(: std::exception) {
  char const * const message;

  IncompatibleLambdaError(char const * message) :
   message(message)
  {}

  char const * what() const noexcept CX_STL_SUPPORT_EXPR(override) {
   return message;
  }
 };

 //Non-allocating lambda
 template<typename>
 struct Lambda;

 //Allocating lambda
 template<typename>
 struct AllocLambda;

 //Lambda meta-functions
 namespace LambdaMetaFunctions {
  namespace Internal {
   template<template<typename> typename T>
   concept IsLambdaTemplate = CX::MatchAnyTemplateType<
    T,
    Lambda,
    AllocLambda
   >;
  }

  template<typename>
  struct IsLambda : FalseType {};

  template<template<typename> typename L, typename R, typename... Args>
  struct IsLambda<L<R (Args...)>> {
   static constexpr auto const Value = Internal::IsLambdaTemplate<L>;
  };

  template<template<typename> typename L, typename R, typename... Args>
  struct IsLambda<L<R (Args...) noexcept>> : IsLambda<L<R (Args...)>> {};

  template<template<typename> typename L, typename R, typename... Args>
  struct IsLambda<L<R (Args..., ...)>> : IsLambda<L<R (Args...)>> {};

  template<template<typename> typename L, typename R, typename... Args>
  struct IsLambda<L<R (Args..., ...) noexcept>> : IsLambda<L<R (Args...)>> {};

  template<typename>
  struct IsNonAllocLambda : FalseType {};

  template<template<typename> typename L, typename Prototype>
  struct IsNonAllocLambda<L<Prototype>> : MetaFunctions::SameTemplateType<L, Lambda> {};

  template<typename>
  struct IsAllocLambda : FalseType {};

  template<template<typename> typename L, typename Prototype>
  struct IsAllocLambda<L<Prototype>> : MetaFunctions::SameTemplateType<L, AllocLambda> {};

  //True conditions:
  // - qualified lambda -> unqualified lambda (both alloc and non-alloc)
  // - no-alloc lambda <-> alloc lambda
  template<typename, typename>
  struct CompatibleLambda : FalseType {};

  //Matching lambda prototype
  template<
   template<typename> typename L1,
   typename Prototype1,
   template<typename> typename L2,
   typename Prototype2
  >
  requires (Internal::IsLambdaTemplate<L1> && Internal::IsLambdaTemplate<L2>)
  struct CompatibleLambda<L1<Prototype1>, L2<Prototype2>> {
   static constexpr auto const Value = CX::SameType<Prototype1, Prototype2>;
  };

  //Noexcept-qualified lambda to unqualified lambda
  template<
   template<typename> typename L1,
   typename Prototype1,
   template<typename> typename L2,
   typename Prototype2
  >
  requires (Internal::IsLambdaTemplate<L1>
   && Internal::IsLambdaTemplate<L2>
   && !CX::NoexceptFunction<Prototype1>
   && CX::NoexceptFunction<Prototype2>
  )
  struct CompatibleLambda<L1<Prototype1>, L2<Prototype2>> : TrueType {};
 }

 //Lambda identity
 template<typename T>
 concept IsLambda = LambdaMetaFunctions
  ::IsLambda<ConstVolatileDecayed<T>>
  ::Value;

 //Non-alloc Lambda identity
 template<typename T>
 concept IsNonAllocLambda =
  LambdaMetaFunctions
   ::IsLambda<ConstVolatileDecayed<T>>
   ::Value
  && LambdaMetaFunctions
   ::IsNonAllocLambda<ConstVolatileDecayed<T>>
   ::Value;

 //AllocLambda identity
 template<typename T>
 concept IsAllocLambda =
  LambdaMetaFunctions
   ::IsLambda<ConstVolatileDecayed<T>>
   ::Value
  && LambdaMetaFunctions
   ::IsAllocLambda<ConstVolatileDecayed<T>>
   ::Value;

 //Compatible lambda types identity (see above)
 template<typename L2, typename L1>
 concept CompatibleLambda = LambdaMetaFunctions
  ::CompatibleLambda<ConstVolatileDecayed<L1>, ConstVolatileDecayed<L2>>
  ::Value;

 //Lambda utilities
 namespace Internal {
  //Utility functions
  [[gnu::always_inline]]
  inline void wrapperBufferCheck(
   decltype(sizeof(0)) srcSize,
   decltype(alignof(int)) srcAlignment,
   decltype(srcSize) dstSize,
   decltype(srcAlignment) dstAlignment
  ) {
   //Check that the receiving buffer is sized properly
   if (dstSize < srcSize) {
    throw IncompatibleLambdaError{
     "Receiving lambda does not have a large enough buffer to contain "
     "the assigned lambda's buffer"
    };
   }

   //Check that the receiving buffer is aligned properly
   if (dstAlignment < srcAlignment) {
    throw IncompatibleLambdaError{
     "Receiving lambda's buffer is not aligned properly to receive "
     "the assigned lambda's buffer"
    };
   }
  }

  //The base of all lambda buffers
  template<typename F>
  struct LambdaBase;

  //Ignore warnings for throwing in noexcept functions;
  //trying to invoke an uninitialized lambda will:
  // - throw for unqualified lambdas
  // - terminate for noexcept lambdas
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wexceptions"

  //Base of unqualified and `noexcept` qualified function types
  template<typename R, typename... Args>
  struct LambdaBase<R (Args...)> {
   template<typename>
   friend struct LambdaBase;

   template<typename>
   friend struct NonAllocLambdaWrapperStub;

   template<typename>
   friend struct AllocLambdaWrapperStub;

   template<typename, typename>
   friend struct NonAllocLambdaWrapper;

   template<typename, typename>
   friend struct AllocLambdaWrapper;

  private:
   LambdaBase() = default;

  public:
   virtual ~LambdaBase() = default;

   [[gnu::always_inline]]
   virtual R op(Args...) {
    throw UninitializedLambdaError{};
   }

   [[gnu::always_inline]]
   virtual R noexceptOp(Args...) noexcept {
    throw UninitializedLambdaError{};
   }

   virtual bool allocates() const noexcept = 0;
   virtual decltype(sizeof(0)) typeSize() const noexcept = 0;
   virtual decltype(alignof(int)) typeAlignment() const noexcept = 0;
   virtual decltype(sizeof(0)) bufferSize() const noexcept = 0;
   virtual decltype(alignof(int)) bufferAlignment() const noexcept = 0;
   //(bufPtr, size, alignment, allocating?)
   virtual void copyTo(
    void **,
    decltype(sizeof(0)),
    decltype(alignof(int)),
    bool
   ) = 0;

   [[gnu::always_inline]]
   virtual bool present() const noexcept {
    return false;
   }
  };

  //Base of unqualified and `noexcept` qualified c-variadic function types
  template<typename R, typename... Args>
  struct LambdaBase<R (Args..., ...)> {
   template<typename>
   friend struct LambdaBase;

   template<typename>
   friend struct NonAllocLambdaWrapperStub;

   template<typename>
   friend struct AllocLambdaWrapperStub;

   template<typename, typename>
   friend struct NonAllocLambdaWrapper;

   template<typename, typename>
   friend struct AllocLambdaWrapper;

  private:
   LambdaBase() = default;

  public:
   using FuncType = R (Args..., ...);
   using NoexceptFuncType = R (Args..., ...) noexcept;

   #pragma GCC diagnostic push
   #pragma GCC diagnostic ignored "-Wgnu-anonymous-struct"
   #pragma GCC diagnostic ignored "-Wnested-anon-types"

   struct FptrWrapper final {
    void * fptrOrInst;
    struct MemberPtr final {
     union {
      void (MemberPtr::* memberPtr)();
      struct {
       void
        * lo,
        * hi;
      };
     };

     MemberPtr() :
      lo(nullptr),
      hi(nullptr)
     {}

     template<CX::MemberFunction F>
     MemberPtr(F memFptr) :
      memberPtr((decltype(memberPtr))memFptr)
     {}
    } memberPtr;

    #pragma GCC diagnostic pop

    FptrWrapper(CX::StaticFunction auto ptr) :
     fptrOrInst((void *)ptr)
    {}

    template<CX::Struct T>
    FptrWrapper(T const &inst) :
     fptrOrInst((void *)&inst),
     memberPtr{&T::operator()}
    {}

    template<bool Noexcept, typename... Varargs>
    [[gnu::always_inline]]
    R invoke(Args... args, Varargs... varargs) noexcept(Noexcept) {
     if (memberPtr.lo || memberPtr.hi) {
      //Invoke member function
      auto const ptr = (R (FptrWrapper::*)(Args..., ...) noexcept(Noexcept))memberPtr.memberPtr;
      return (((FptrWrapper *)fptrOrInst)->*ptr)(args..., varargs...);
     } else {
      //Invoke static function
      return ((R (*)(Args..., ...) noexcept(Noexcept))fptrOrInst)(args..., varargs...);
     }
    }
   };

   virtual ~LambdaBase() = default;

   virtual FptrWrapper get() {
    throw UninitializedLambdaError{};
   }

   template<bool Noexcept, typename... Varargs>
   [[gnu::always_inline]]
   R op(Args... args, Varargs... varargs) noexcept(Noexcept) {
    return get().template invoke<Noexcept, Varargs...>(args..., varargs...);
   }

   virtual bool allocates() const noexcept = 0;
   virtual decltype(sizeof(0)) typeSize() const noexcept = 0;
   virtual decltype(alignof(int)) typeAlignment() const noexcept = 0;
   virtual decltype(sizeof(0)) bufferSize() const noexcept = 0;
   virtual decltype(alignof(int)) bufferAlignment() const noexcept = 0;
   //(bufPtr, size, alignment, allocating?)
   virtual void copyTo(
    void **,
    decltype(sizeof(0)),
    decltype(alignof(int)),
    bool
   ) = 0;

   [[gnu::always_inline]]
   virtual bool present() const noexcept {
    return false;
   }
  };

  #pragma GCC diagnostic pop

  //Implementation of `LambdaBase<...>` for uninitialized
  //non-allocating lambdas
  template<typename Prototype>
  struct NonAllocLambdaWrapperStub;

  //Implementation of `LambdaBase<...>` for uninitialized
  //allocating lambdas
  template<typename Prototype>
  struct AllocLambdaWrapperStub;

  //Implementation of `LambdaBase<...>` for non-allocating lambdas
  template<typename, typename>
  struct NonAllocLambdaWrapper;

  //Implementation of `LambdaBase<...>` for allocating lambdas
  template<typename, typename>
  struct AllocLambdaWrapper;

  //Check `F` against a restriction
  template<typename F, template<typename> typename Restriction>
  concept SupportedPrototype = requires (Restriction<F> r) {
   r;
  };

  //LambdaOperationBase implementation; provides implementations for all
  //underlying lambda mechanisms, for all specializations.
  template<
   //The static function prototype of the invoking lambda specialization
   typename SpecializationPrototype,
   //Prototype restrictions for the invoking lambda specialization
   template<typename> typename Restriction,
   bool Allocating
  >
  struct LambdaOperationBase {
  private:
   using EmptyWrapperType = CX::SelectType<
    Allocating,
    AllocLambdaWrapperStub<SpecializationPrototype>,
    NonAllocLambdaWrapperStub<SpecializationPrototype>
   >;

   template<typename F, typename Prototype>
   using WrapperType = CX::SelectType<
    Allocating,
    AllocLambdaWrapper<F, Prototype>,
    NonAllocLambdaWrapper<F, Prototype>
   >;

   template<typename>
   friend struct NonAllocLambdaWrapperStub;

   template<typename>
   friend struct AllocLambdaWrapperStub;

   template<typename, typename>
   friend struct NonAllocLambdaWrapper;

   template<typename, typename>
   friend struct AllocLambdaWrapper;

   #ifdef CX_STL_SUPPORT
    template<typename Wrapper>
    [[gnu::always_inline]]
    static void checkOrReallocate(
     void ** buffer,
     decltype(sizeof(0)) &size,
     decltype(alignof(int)) &alignment
    ) {
     constexpr auto const
      wrapperSize = sizeof(Wrapper),
      wrapperAlignment = alignof(Wrapper);

     if (size == 0 && alignment == 0) {
      //Note: `size == 0 && alignment == 0` is a special case
      //for the initial `AllocLambda` buffer allocation. Will
      //try to use `CX_LAMBDA_BUF_SIZE` and `CX_LAMBDA_BUF_ALIGN`
      //for the allocation, to minimize the number of
      //re-allocations.

      //Determine new buffer properties
      size = std::max(CX_LAMBDA_BUF_SIZE, wrapperSize);
      alignment = std::max(CX_LAMBDA_BUF_ALIGN, wrapperAlignment);

      //Allocate new buffer
      *buffer = operator new(
       size,
       (std::align_val_t)alignment
      );
     } else if (size < wrapperSize || alignment < wrapperAlignment) {
      //If the buffer does not meet the `wrapper` size and
      //alignment requirements, allocate new buffer with
      //requiements met
      *buffer = operator new(
       wrapperSize,
       (std::align_val_t)wrapperAlignment
      );

      //Set new buffer properties
      size = wrapperSize;
      alignment = wrapperAlignment;
     }
    }
   #endif //CX_STL_SUPPORT

   //Copy-constructs or default constructs and copy-assigns
   //`Wrapper` with `Function`, inside the provided buffer,
   //`buf`
   template<typename Wrapper, typename Function>
   requires (CopyConstructible<Function>
    || (Constructible<Function> && CopyAssignable<Function>)
   )
   [[gnu::always_inline]]
   static void copyInitWrapper(
    void * buf,
    decltype(sizeof(0)) bufSize,
    decltype(alignof(int)) bufAlignment,
    Function const &func
   ) {
    if constexpr (!Allocating) {
     //Ensure receiving buffer is compatible
     wrapperBufferCheck(
      sizeof(Wrapper),
      alignof(Wrapper),
      bufSize,
      bufAlignment
     );
    }

    //Initialize buffer
    if constexpr (CopyConstructible<Function>) {
     //If `Function` is copy constructible, invoke wrapper
     //function copy constructor
     new (buf) Wrapper{(Function const&)func, bufSize, bufAlignment};
    } else if constexpr(Constructible<Function> && CopyAssignable<Function>) {
     //If `Function` is default constructible and copy assignable,
     //default construct wrapper and copy assign to function
     (*new (buf) Wrapper{bufSize, bufAlignment}).f = (Function const&)func;
    }
   }

   //Move-constructs or default constructs and move-assigns
   //`Wrapper` with `Function`, inside the provided buffer,
   //`buf`
   template<typename Wrapper, typename Function>
   requires (MoveConstructible<Function>
    || (Constructible<Function> && MoveAssignable<Function>)
   )
   [[gnu::always_inline]]
   static void moveInitWrapper(
    void * buf,
    decltype(sizeof(0)) bufSize,
    decltype(alignof(int)) bufAlignment,
    Function &&func
   ) {
    if constexpr (!Allocating) {
     //Ensure receiving buffer is compatible
     wrapperBufferCheck(
      sizeof(Wrapper),
      alignof(Wrapper),
      bufSize,
      bufAlignment
     );
    }

    //Initialize buffer
    if constexpr(MoveConstructible<Function>) {
     //If `Function` is move constructible, invoke wrapper
     //function move constructor
     new (buf) Wrapper{(Function&&)func, bufSize, bufAlignment};
    } else if constexpr (Constructible<Function> && MoveAssignable<Function>) {
     //If `Function` is default constructible and move assignable,
     //default construct wrapper and move assign to function
     (*new (buf) Wrapper{bufSize, bufAlignment}).f = (Function&&)func;
    }
   }

   template<
    typename F,
    bool Copy,
    IsLambda L
   >
   requires (SupportedPrototype<
    ConstDecayed<ReferenceDecayed<F>>,
    Restriction
   >)
   [[gnu::always_inline]]
   static void assignFunctionCommon(L &l, F f) {
    using WrapperSpecialization = WrapperType<
     ConstDecayed<ReferenceDecayed<F>>,
     SpecializationPrototype
    >;

    void ** lBufPtr;
    decltype(sizeof(0)) lSize;
    decltype(alignof(int)) lAlignment;

    //Fetch existing buffer size and alignment
    auto const getProperties = [&]() {
     auto &lBase = **(LambdaBase<SpecializationPrototype> **)lBufPtr;
     lSize = lBase.bufferSize();
     lAlignment = lBase.bufferAlignment();
    };

    //Perform assignment through `op`
    auto const assign = [&]() {
     //Clean up `l`'s buffer
     destroy(*lBufPtr);

     //Copy or move initialize `WrapperType`
     if constexpr (Copy) {
      copyInitWrapper<WrapperSpecialization>(
       *lBufPtr,
       lSize,
       lAlignment,
       (F)f
      );
     } else {
      moveInitWrapper<WrapperSpecialization>(
       *lBufPtr,
       lSize,
       lAlignment,
       (F)f
      );
     }
    };

    //Deal with `AllocLambda<...>`
    #ifdef CX_STL_SUPPORT
     if constexpr (CX::IsAllocLambda<L>) {
      allocLambdaBufferOp(l.buffer, [&](void ** ptr) {
       lBufPtr = ptr;
       getProperties();
       checkOrReallocate<WrapperSpecialization>(
        lBufPtr,
        lSize,
        lAlignment
       );
       assign();
      });
      return;
     }
    #endif //CX_STL_SUPPORT

    //Deal with `Lambda<...>`
    if constexpr (CX::IsNonAllocLambda<L>) {
     void * ptr = &l.buf();
     lBufPtr = &ptr;
     getProperties();
     assign();
     return;
    }
   }

  public:
   //Utility function for non-alloc lambda; `Lambda<...>`
   //Ensure `T` will fit within a buffer of size `Size` with
   //alignment `Alignment`
   template<typename T, auto Size, auto Alignment>
   [[gnu::always_inline]]
   static void lambdaAssignBufferCheck() {
    //Error if check used in incorrect context
    static_assert(
     !Allocating,
     "Do not invoke `lambdaAssignBufferCheck` for allocating lambda "
     "specializations, as they do not have strict size or alignment "
     "limits."
    );

    //Note: Add `alognof(void *)` to account for internal
    //requirements
    static_assert(
     alignof(NonAllocLambdaWrapper<T, SpecializationPrototype>) <= Alignment,
     "The required alignment of the given functor is larger than the "
     "current 'CX_LAMBDA_BUF_ALIGN' value. Consider increasing it to "
     "the next power of 2."
    );
    //Note: Add `sizeof(void *)` to account for vtable ptr
    static_assert(
     alignof(NonAllocLambdaWrapper<T, SpecializationPrototype>) <= Size,
     "The size of the given functor is larger than the current "
     "'CX_LAMBDA_BUF_SIZE' value. Consider increasing it to the next "
     "power of 2."
    );
   }

   //Utilities for `AllocLambda`
   #ifdef CX_STL_SUPPORT
    //Aligned deleter for `AllocLambda<...>`'s underlying `shared_ptr`
    //buffer
    static void sharedBufferDeleter(unsigned char * ptr) {
     if (ptr) {
      auto &base = *(LambdaBase<SpecializationPrototype> *)ptr;
      auto const alignment = base.bufferAlignment();

      //Destruct buffer
      destroy(ptr);

      //Delete aligned buffer
      operator delete(ptr, (std::align_val_t)alignment);
     }
    };

    //Buffer manipulation pattern utility
    static void allocLambdaBufferOp(std::shared_ptr<unsigned char> &buf, auto op) {
     unsigned char * bufPtr = buf.get();
     op((void **)&bufPtr);

     //If `bufPtr` was re-allocated, reset `buf`
     if (bufPtr != buf.get()) {
      buf.reset(bufPtr, sharedBufferDeleter);
     }
    }
   #endif

   //Initializes buffer with empty `LambdaBase` obj
   [[gnu::always_inline]]
   static void initEmptyLambda(
    void * buffer,
    decltype(sizeof(0)) size,
    decltype(alignof(int)) alignment
   ) {
    auto bufPtr = Allocating ? (void **)buffer : &buffer;
    if constexpr (Allocating) {
     //Allocate aligned buffer for `AllocLambda`
     #ifdef CX_STL_SUPPORT
      //Allocate aligned buffer
      checkOrReallocate<EmptyWrapperType>(
       bufPtr,
       size,
       alignment
      );
     #endif //CX_STL_SUPPORT
    } else {
     //Check buffer constraints for non-alloc `Lambda`
     //Ensure receiving buffer is compatible
     wrapperBufferCheck(
      sizeof(EmptyWrapperType),
      alignof(EmptyWrapperType),
      size,
      alignment
     );
    }

    //Initialize buffer
    new (*bufPtr) EmptyWrapperType {
     size,
     alignment
    };
   }

   //Destructs current `LambdaBase` obj in buffer
   [[gnu::always_inline]]
   static void destroy(void * buffer) {
    using LambdaBase = LambdaBase<SpecializationPrototype>;
    (*(LambdaBase *)buffer).~LambdaBase();
   }

   //FunctionOperator move assignment
   template<SupportedPrototype<Restriction> F>
   requires (CopyConstructible<F>
    || (Constructible<F> && CopyAssignable<F>)
   )
   [[gnu::always_inline]]
   static void copyAssignFunction(IsLambda auto &l, F const &f) {
    assignFunctionCommon<F const&, true>(l, f);
   }

   //FunctionOperator / function pointer copy assignment
   template<SupportedPrototype<Restriction> F>
   requires (MoveConstructible<F>
    || (Constructible<F> && MoveAssignable<F>)
   )
   [[gnu::always_inline]]
   static void moveAssignFunction(IsLambda auto &l, F &&f) {
    assignFunctionCommon<F&&, false>(l, (F&&)f);
   }

   //Lambda copy and move assignment
   template<
    typename L2,
    typename L1 = SelectType<
     Allocating,
     AllocLambda<SpecializationPrototype>,
     Lambda<SpecializationPrototype>
    >
   >
   requires (CompatibleLambda<L2, L1>)
   [[gnu::always_inline]]
   static void copyAssignLambda(L1 &l1, L2 const &l2) {
    //Copies the wrapper instance from `buf2` to `buf1`
    constexpr auto const copyBuffer = [](void * buf1, void * buf2) {
     void ** buf1Ptr = (Allocating ? (void **)buf1 : &buf1);
     decltype(sizeof(0)) buf1Size;
     decltype(alignof(int)) buf1Alignment;

     //Fetch `buf1` size and alignment
     {
      auto &l1Base = **(LambdaBase<SpecializationPrototype> **)buf1Ptr;
      buf1Size = l1Base.bufferSize();
      buf1Alignment = l1Base.bufferAlignment();
     }

     //Clean up `buf1` state
     destroy(*buf1Ptr);

     //Copy `buf2` to `buf1`
     auto &l2Base = *(LambdaBase<SpecializationPrototype> *)buf2;
     l2Base.copyTo(
      buf1Ptr,
      buf1Size,
      buf1Alignment,
      Allocating
     );
    };

    //`Lambda<...> <- Lambda<...>`
    if constexpr(IsNonAllocLambda<L1> && IsNonAllocLambda<L2>) {
     copyBuffer(&l1.buf(), &l2.buf());
    }

    #ifdef CX_STL_SUPPORT
     //`AllocLambda<...> <- AllocLambda<...>`
     if constexpr (IsAllocLambda<L1> && IsAllocLambda<L2>) {
      //The alloc lambda uses a ref-counted buffer; no need to
      //re-allocate and construct a new wrapper, etc...
      l1.buffer = l2.buffer;
     }

     //`Lambda<...> <- AllocLambda<...>`
     if constexpr (IsNonAllocLambda<L1> && IsAllocLambda<L2>) {
      copyBuffer(&l1.buf(), &l2.buf());
     }

     //`AllocLambda<...> <- Lambda<...>`
     if constexpr (IsAllocLambda<L1> && IsNonAllocLambda<L2>) {
      allocLambdaBufferOp(l1.buffer, [&](void ** bufPtr) {
       copyBuffer(bufPtr, &l2.buf());
      });
     }
    #endif //CX_STL_SUPPORT
   }

   //Lambda move assignment
   template<
    typename L2,
    typename L1 = SelectType<
     Allocating,
     AllocLambda<SpecializationPrototype>,
     Lambda<SpecializationPrototype>
    >
   >
   requires (CompatibleLambda<L2, L1>)
   [[gnu::always_inline]]
   static void moveAssignLambda(L1 &l1, L2 &l2) {
    //Moves the wrapper instance from `buf2` to `buf1`
    constexpr auto const moveBuffer = [](void * buf1, void * buf2) {
     void ** buf1Ptr = (Allocating ? (void **)buf1 : &buf1);
     decltype(sizeof(0))
      buf1Size,
      buf2Size;
     decltype(alignof(int))
      buf1Alignment,
      buf2Alignment;

     //Fetch `buf1` size and alignment
     {
      auto &l1Base = **(LambdaBase<SpecializationPrototype> **)buf1Ptr;
      buf1Size = l1Base.bufferSize();
      buf1Alignment = l1Base.bufferAlignment();
     }

     //Clean up `buf1` state
     destroy(*buf1Ptr);

     //Fetch `buf2` size and alignment and copy to `buf1`
     {
      auto &l2Base = *(LambdaBase<SpecializationPrototype> *)buf2;

      //Fetch size and alignment
      buf2Size = l2Base.bufferSize();
      buf2Alignment = l2Base.bufferAlignment();

      //Copy `buf2` to `buf1`
      l2Base.copyTo(
       buf1Ptr,
       buf1Size,
       buf1Alignment,
       Allocating
      );
     }

     //Clean up `buf2` state and reinitialize it as an empty
     //lambda
     destroy(buf2);
     //Note: `LambdaOperationBase<...>::initEmptyLambda(...)` changes
     //its behaviour based on the `Allocating` non-type template flag
     //so the re-intialization of `l2` needs to switch
     //`LambdaOperationBase` partial specializations based on the Lambda
     //type of `L2`; either `AllocLambda` -> `true` or
     //`Lambda` -> `false`
     LambdaOperationBase<
      SpecializationPrototype,
      Restriction,
      IsAllocLambda<L2>
     >::initEmptyLambda(
      (IsAllocLambda<L2> ? &buf2 : buf2),
      buf2Size,
      buf2Alignment
     );
    };

    //`Lambda<...> <- Lambda<...>`
    if constexpr(IsNonAllocLambda<L1> && IsNonAllocLambda<L2>) {
     moveBuffer(&l1.buf(), &l2.buf());
    }

    #ifdef CX_STL_SUPPORT
     //`AllocLambda<...> <- AllocLambda<...>`
     if constexpr (IsAllocLambda<L1> && IsAllocLambda<L2>) {
      //Copy `l2` buffer to `l1` and reset `l2` as an empty lambda

      //Note: The alloc lambda uses a ref-counted buffer; no need
      //to re-allocate and construct a new wrapper, etc...
      l1.buffer = l2.buffer;
      l2.buffer.reset();

      //Re-initialize `l2` as an empty lambda
      allocLambdaBufferOp(l2.buffer, [](void ** bufPtr) {
       initEmptyLambda(
        bufPtr,
        0,
        0
       );
      });
     }

     //`Lambda<...> <- AllocLambda<...>`
     if constexpr (IsNonAllocLambda<L1> && IsAllocLambda<L2>) {
      moveBuffer(&l1.buf(), l2.buffer.get());
     }

     //`AllocLambda<...> <- Lambda<...>`
     if constexpr (IsAllocLambda<L1> && IsNonAllocLambda<L2>) {
      allocLambdaBufferOp(l1.buffer, [&](void ** bufPtr) {
       moveBuffer(bufPtr, &l2.buf());
      });
     }
    #endif //CX_STL_SUPPORT
   }
  };

  //Stub implementation of `LambdaBase<...>` for all
  //lambda prototypes; default instance for uninitialized
  //non-allocating lambdas
  template<typename Prototype>
  struct NonAllocLambdaWrapperStub : LambdaBase<Prototype> {
  private:
   using LambdaBase = LambdaBase<Prototype>;

   //Note: Use `CX::Dummy<...>` as the restriction since it
   //will pass all parameters; at this point, the encapsulated
   //type, `F`, has already been checked against the expected
   //restriction, at compile-time.
   template<typename>
   using Restriction = Dummy<>;

   using OperationBase = LambdaOperationBase<
    Prototype,
    Restriction,
    false
   >;

   decltype(sizeof(0)) bufSize;
   decltype(alignof(int)) bufAlignment;

  public:
   NonAllocLambdaWrapperStub(
    decltype(bufSize) size,
    decltype(bufAlignment) alignment
   ) :
    LambdaBase{},
    bufSize(size),
    bufAlignment(alignment)
   {}

   bool allocates() const noexcept override {
    return false;
   }

   decltype(sizeof(0)) typeSize() const noexcept override {
    return sizeof(NonAllocLambdaWrapperStub);
   }

   decltype(alignof(int)) typeAlignment() const noexcept override {
    return alignof(NonAllocLambdaWrapperStub);
   }

   decltype(sizeof(0)) bufferSize() const noexcept override {
    return bufSize;
   }

   decltype(alignof(int)) bufferAlignment() const noexcept override {
    return bufAlignment;
   }

   void copyTo(
    void ** dstBufPtr,
    decltype(sizeof(0)) dstBufSize,
    decltype(alignof(int)) dstBufAlignment,
    bool allocating
   ) override {
    if (allocating) {
     #ifdef CX_STL_SUPPORT
      //Check before copying NonAllocLambdaWrapperStub to AllocLambda buffer
      LambdaOperationBase<
       Prototype,
       Restriction,
       true
      >::template checkOrReallocate<NonAllocLambdaWrapperStub>(
       dstBufPtr,
       dstBufSize,
       dstBufAlignment
      );

      //Initialize allocated buffer with AllocLambdaWrapperStub and
      //buffer properties
      new (*dstBufPtr) AllocLambdaWrapperStub<Prototype>{
       dstBufSize,
       dstBufAlignment
      };
     #endif //CX_STL_SUPPORT
    } else {
     //Check before copying NonAllocLambdaWrapperStub to Lambda buffer
     //Ensure receiving buffer is compatible
     wrapperBufferCheck(
      typeSize(),
      typeAlignment(),
      dstBufSize,
      dstBufAlignment
     );

     //Initialize non-allocated buffer with NonAllocLambdaWrapperStub
     //and the default non-allocated lambda buffer properties
     new (*dstBufPtr) NonAllocLambdaWrapperStub{
      dstBufSize,
      dstBufAlignment
     };
    }
   }
  };

  //Checks to ensure that the non-allocated lambda buffers are
  //sufficiently sized and aligned for internal overhead
  static_assert(
   CX_LAMBDA_BUF_ALIGN >= alignof(NonAllocLambdaWrapperStub<void ()>),
   "'CX_LAMBDA_BUF_ALIGN' must be at least as large as "
   "`alignof(NonAllocLambdaWrapperStub<void ()>)` to compensate for "
   "internal overhead."
  );
  static_assert(
   CX_LAMBDA_BUF_SIZE >= sizeof(NonAllocLambdaWrapperStub<void ()>),
   "'CX_LAMBDA_BUF_SIZE' must be at least as large as "
   "`sizeof(NonAllocLambdaWrapperStub<void ()>)` to compensate for "
   "internal overhead."
  );

  //`AllocLambda` stub wrapper implementation
  #ifdef CX_STL_SUPPORT
   //Stub implementation of `LambdaBase<...>` for all
   //lambda prototypes; default instance for uninitialized
   //allocating lambdas
   template<typename Prototype>
   struct AllocLambdaWrapperStub : NonAllocLambdaWrapperStub<Prototype> {
   private:
    using LambdaBase = LambdaBase<Prototype>;

    //Note: Use `CX::Dummy<...>` as the restriction since it
    //will pass all parameters; at this point, the encapsulated
    //type, `F`, has already been checked against the expected
    //restriction, at compile-time.
    template<typename>
    using Restriction = Dummy<>;

    using OperationBase = LambdaOperationBase<
     Prototype,
     Restriction,
     true
    >;

   public:
    using NonAllocLambdaWrapperStub<Prototype>::NonAllocLambdaWrapperStub;

    bool allocates() const noexcept override {
     return true;
    }

    decltype(sizeof(0)) typeSize() const noexcept override {
     return sizeof(AllocLambdaWrapperStub);
    }

    decltype(alignof(int)) typeAlignment() const noexcept override {
     return alignof(AllocLambdaWrapperStub);
    }

    void copyTo(
     void ** dstBufPtr,
     decltype(sizeof(0)) dstBufSize,
     decltype(alignof(int)) dstBufAlignment,
     bool allocating
    ) override {
     if (allocating) {
      //Check before copying AllocLambdaWrapperStub to AllocLambda buffer
      OperationBase::template checkOrReallocate<AllocLambdaWrapperStub>(
       dstBufPtr,
       dstBufSize,
       dstBufAlignment
      );

      //Initialize allocated buffer with AllocLambdaWrapperStub and
      //buffer properties
      new (*dstBufPtr) AllocLambdaWrapperStub{
       dstBufSize,
       dstBufAlignment
      };
     } else {
      using NonAllocWrapperStub = NonAllocLambdaWrapperStub<Prototype>;

      //Check before copying NonAllocLambdaWrapperStub to Lambda buffer
      wrapperBufferCheck(
       sizeof(NonAllocWrapperStub),
       alignof(NonAllocWrapperStub),
       dstBufSize,
       dstBufAlignment
      );

      //Initialize non-allocated buffer with NonAllocLambdaWrapperStub
      //and the buffer properties
      new (*dstBufPtr) NonAllocWrapperStub{
       dstBufSize,
       dstBufAlignment
      };
     }
    }
   };
  #endif //CX_STL_SUPPORT

  //Lambda wrapper for `F` (non-c-variadic)
  template<typename F, typename R, typename... Args>
  struct NonAllocLambdaWrapper<F, R (Args...)> : LambdaBase<R (Args...)> {
  private:
   using Prototype = R (Args...);
   using LambdaBase = LambdaBase<Prototype>;

   //Note: Use `CX::Dummy<...>` as the restriction since it
   //will pass all parameters; at this point, the encapsulated
   //type, `F`, has already been checked against the expected
   //restriction, at compile-time.
   template<typename>
   using Restriction = Dummy<>;

   using OperationBase = LambdaOperationBase<
    Prototype,
    Restriction,
    false
   >;

  public:
   F f;

   //Default constructor
   NonAllocLambdaWrapper(
    decltype(sizeof(0)),
    decltype(alignof(int))
   ) :
    LambdaBase{}
   {};

   //Function copy constructor
   //Note: size and alignment arguments are unused for the
   //non-allocating lambda specialization
   NonAllocLambdaWrapper(
    F const &f,
    decltype(sizeof(0)),
    decltype(alignof(int))
   ) :
    LambdaBase{},
    f((F const&)f)
   {}

   //Function move constructor
   NonAllocLambdaWrapper(
    F &&f,
    decltype(sizeof(0)),
    decltype(alignof(int))
   ) :
    LambdaBase{},
    f((F&&)f)
   {}

   [[gnu::always_inline]]
   R op(Args... args) override {
    return f(args...);
   }

   [[gnu::always_inline]]
   R noexceptOp(Args... args) noexcept override {
    return f(args...);
   }

   bool allocates() const noexcept override {
    return false;
   }

   decltype(sizeof(0)) typeSize() const noexcept override {
    return sizeof(NonAllocLambdaWrapper);
   }

   decltype(alignof(int)) typeAlignment() const noexcept override {
    return alignof(NonAllocLambdaWrapper);
   }

   decltype(sizeof(0)) bufferSize() const noexcept override {
    return CX_LAMBDA_BUF_SIZE;
   }

   decltype(alignof(int)) bufferAlignment() const noexcept override {
    return CX_LAMBDA_BUF_ALIGN;
   }

   void copyTo(
    void ** dstBufPtr,
    decltype(sizeof(0)) dstBufSize,
    decltype(alignof(int)) dstBufAlignment,
    bool allocating
   ) override {
    if (allocating) {
     #ifdef CX_STL_SUPPORT
      using AllocLambdaWrapper = AllocLambdaWrapper<F, Prototype>;
      using AllocOperationBase = LambdaOperationBase<
       Prototype,
       Restriction,
       true
      >;

      //Check before copying NonAllocLambdaWrapper to AllocLambda buffer
      AllocOperationBase::template checkOrReallocate<AllocLambdaWrapper>(
       dstBufPtr,
       dstBufSize,
       dstBufAlignment
      );

      //Initialize allocated buffer with AllocLambdaWrapper and
      //new buffer properties
      AllocOperationBase::template copyInitWrapper<AllocLambdaWrapper>(
       *dstBufPtr,
       dstBufSize,
       dstBufAlignment,
       (F const&)f
      );

     #endif //CX_STL_SUPPORT
    } else {
     //Copy encapsulated function to Lambda buffer
     OperationBase::template copyInitWrapper<NonAllocLambdaWrapper>(
      *dstBufPtr,
      dstBufSize,
      dstBufAlignment,
      (F const&)f
     );
    }
   }

   [[gnu::always_inline]]
   bool present() const noexcept override {
    return true;
   }
  };

  //Lambda wrapper for `F` (c-variadic)
  template<typename F, typename R, typename... Args>
  struct NonAllocLambdaWrapper<F, R (Args..., ...)> : LambdaBase<R (Args..., ...)> {
  private:
   using Prototype = R (Args..., ...);
   using LambdaBase = LambdaBase<Prototype>;

   //Note: Use `CX::Dummy<...>` as the restriction since it
   //will pass all parameters; at this point, the encapsulated
   //type, `F`, has already been checked against the expected
   //restriction, at compile-time.
   template<typename>
   using Restriction = Dummy<>;

   using OperationBase = LambdaOperationBase<
    Prototype,
    Restriction,
    false
   >;

  public:
   F f;

   //Default constructor
   NonAllocLambdaWrapper(
    decltype(sizeof(0)),
    decltype(alignof(int))
   ) :
    LambdaBase{}
   {};

   //Function copy constructor
   NonAllocLambdaWrapper(
    F const &f,
    decltype(sizeof(0)),
    decltype(alignof(int))
   ) :
    LambdaBase{},
    f((F const&)f)
   {}

   //Function move constructor
   NonAllocLambdaWrapper(
    F &&f,
    decltype(sizeof(0)),
    decltype(alignof(int))
   ) :
    LambdaBase{},
    f((F&&)f)
   {}

   [[gnu::always_inline]]
   typename LambdaBase::FptrWrapper get() override {
    return {(F const&)f};
   }

   bool allocates() const noexcept override {
    return false;
   }

   decltype(sizeof(0)) typeSize() const noexcept override {
    return sizeof(NonAllocLambdaWrapper);
   }

   decltype(alignof(int)) typeAlignment() const noexcept override {
    return alignof(NonAllocLambdaWrapper);
   }

   decltype(sizeof(0)) bufferSize() const noexcept override {
    return CX_LAMBDA_BUF_SIZE;
   }

   decltype(alignof(int)) bufferAlignment() const noexcept override {
    return CX_LAMBDA_BUF_ALIGN;
   }

   void copyTo(
    void ** dstBufPtr,
    decltype(sizeof(0)) dstBufSize,
    decltype(alignof(int)) dstBufAlignment,
    bool allocating
   ) override {
    if (allocating) {
     #ifdef CX_STL_SUPPORT
      using AllocLambdaWrapper = AllocLambdaWrapper<F, Prototype>;
      using AllocOperationBase = LambdaOperationBase<
       Prototype,
       Restriction,
       true
      >;

      //Check before copying NonAllocLambdaWrapper to AllocLambda buffer
      AllocOperationBase::template checkOrReallocate<AllocLambdaWrapper>(
       dstBufPtr,
       dstBufSize,
       dstBufAlignment
      );

      //Initialize allocated buffer with AllocLambdaWrapper and
      //new buffer properties
      AllocOperationBase::template copyInitWrapper<AllocLambdaWrapper>(
       *dstBufPtr,
       dstBufSize,
       dstBufAlignment,
       (F const&)f
      );
     #endif //CX_STL_SUPPORT
    } else {
     //Copy encapsulated function to new buffer
     OperationBase::template copyInitWrapper<NonAllocLambdaWrapper>(
      *dstBufPtr,
      dstBufSize,
      dstBufAlignment,
      (F const&)f
     );
    }
   }

   [[gnu::always_inline]]
   bool present() const noexcept override {
    return true;
   }
  };

  //`AllocLambda` wrapper implementations
  #ifdef CX_STL_SUPPORT
   //AllocLambda wrapper for `F` (non-c-variadic)
   template<typename F, typename R, typename... Args>
   struct AllocLambdaWrapper<F, R (Args...)> : LambdaBase<R (Args...)> {
   private:
    using Prototype = R (Args...);
    using LambdaBase = LambdaBase<Prototype>;

    //Note: Use `CX::Dummy<...>` as the restriction since it
    //will pass all parameters; at this point, the encapsulated
    //type, `F`, has already been checked against the expected
    //restriction, at compile-time.
    template<typename>
    using Restriction = Dummy<>;

    using OperationBase = LambdaOperationBase<
     Prototype,
     Restriction,
     true
    >;

    F f;
    decltype(sizeof(0)) bufSize;
    decltype(alignof(int)) bufAlignment;

   public:
    //Default constructor
    AllocLambdaWrapper(
     decltype(bufSize) size,
     decltype(bufAlignment) alignment
    ) :
     LambdaBase{},
     bufSize(size),
     bufAlignment(alignment)
    {};

    //Function copy constructor
    AllocLambdaWrapper(
     F const &f,
     decltype(bufSize) size,
     decltype(bufAlignment) alignment
    ) :
     LambdaBase{},
     f((F const&)f),
     bufSize(size),
     bufAlignment(alignment)
    {}

    //Function move constructor
    AllocLambdaWrapper(
     F &&f,
     decltype(bufSize) size,
     decltype(bufAlignment) alignment
    ) :
     LambdaBase{},
     f((F&&)f),
     bufSize(size),
     bufAlignment(alignment)
    {}

    [[gnu::always_inline]]
    R op(Args... args) override {
     return f(args...);
    }

    [[gnu::always_inline]]
    R noexceptOp(Args... args) noexcept override {
     return f(args...);
    }

    bool allocates() const noexcept override {
     return true;
    }

    decltype(sizeof(0)) typeSize() const noexcept override {
     return sizeof(AllocLambdaWrapper);
    }

    decltype(alignof(int)) typeAlignment() const noexcept override {
     return alignof(AllocLambdaWrapper);
    }

    decltype(sizeof(0)) bufferSize() const noexcept override {
     return bufSize;
    }

    decltype(alignof(int)) bufferAlignment() const noexcept override {
     return bufAlignment;
    }

    void copyTo(
     void ** dstBufPtr,
     decltype(sizeof(0)) dstBufSize,
     decltype(alignof(int)) dstBufAlignment,
     bool allocating
    ) override {
     if (allocating) {
      //Check before copying AllocLambdaWrapper to AllocLambda buffer
      OperationBase::template checkOrReallocate<AllocLambdaWrapper>(
       dstBufPtr,
       dstBufSize,
       dstBufAlignment
      );

      //Initialize allocated buffer with AllocLambdaWrapper and
      //new buffer properties
      OperationBase::template copyInitWrapper<AllocLambdaWrapper>(
       *dstBufPtr,
       dstBufSize,
       dstBufAlignment,
       (F const&)f
      );
     } else {
      using NonAllocWrapper = NonAllocLambdaWrapper<F, Prototype>;

      //Check before copying NonAllocLambdaWrapper to Lambda buffer
      wrapperBufferCheck(
       sizeof(NonAllocWrapper),
       alignof(NonAllocWrapper),
       dstBufSize,
       dstBufAlignment
      );

      //Initialize non-allocated buffer with NonAllocLambdaWrapper
      //and buffer properties
      LambdaOperationBase<
       Prototype,
       Restriction,
       false
      >::template copyInitWrapper<NonAllocWrapper>(
       *dstBufPtr,
       dstBufSize,
       dstBufAlignment,
       (F const&)f
      );
     }
    }

    [[gnu::always_inline]]
    bool present() const noexcept override {
     return true;
    }
   };

   //AllocLambda wrapper for `F` (c-variadic)
   template<typename F, typename R, typename... Args>
   struct AllocLambdaWrapper<F, R (Args..., ...)> : LambdaBase<R (Args..., ...)> {
   private:
    using Prototype = R (Args..., ...);
    using LambdaBase = LambdaBase<Prototype>;

    //Note: Use `CX::Dummy<...>` as the restriction since it
    //will pass all parameters; at this point, the encapsulated
    //type, `F`, has already been checked against the expected
    //restriction, at compile-time.
    template<typename>
    using Restriction = Dummy<>;

    using OperationBase = LambdaOperationBase<
     Prototype,
     Restriction,
     true
    >;

    F f;
    decltype(sizeof(0)) bufSize;
    decltype(alignof(int)) bufAlignment;

   public:
    //Default constructor
    AllocLambdaWrapper(
     decltype(bufSize) size,
     decltype(bufAlignment) alignment
    ) :
     LambdaBase{},
     bufSize(size),
     bufAlignment(alignment)
    {};

    //Function copy constructor
    AllocLambdaWrapper(
     F const &f,
     decltype(bufSize) size,
     decltype(bufAlignment) alignment
    ) :
     LambdaBase{},
     f((F const&)f),
     bufSize(size),
     bufAlignment(alignment)
    {}

    //Function move constructor
    AllocLambdaWrapper(
     F &&f,
     decltype(bufSize) size,
     decltype(bufAlignment) alignment
    ) :
     LambdaBase{},
     f((F&&)f),
     bufSize(size),
     bufAlignment(alignment)
    {}

    [[gnu::always_inline]]
    typename LambdaBase::FptrWrapper get() override {
     return {(F const&)f};
    }

    bool allocates() const noexcept override {
     return true;
    }

    decltype(sizeof(0)) typeSize() const noexcept override {
     return sizeof(AllocLambdaWrapper);
    }

    decltype(alignof(int)) typeAlignment() const noexcept override {
     return alignof(AllocLambdaWrapper);
    }

    decltype(sizeof(0)) bufferSize() const noexcept override {
     return bufSize;
    }

    decltype(alignof(int)) bufferAlignment() const noexcept override {
     return bufAlignment;
    }

    void copyTo(
     void ** dstBufPtr,
     decltype(sizeof(0)) dstBufSize,
     decltype(alignof(int)) dstBufAlignment,
     bool allocating
    ) override {
     if (allocating) {
      //Check before copying AllocLambdaWrapper to AllocLambda buffer
      OperationBase::template checkOrReallocate<AllocLambdaWrapper>(
       dstBufPtr,
       dstBufSize,
       dstBufAlignment
      );

      //Initialize allocated buffer with AllocLambdaWrapper and
      //new buffer properties
      OperationBase::template copyInitWrapper<AllocLambdaWrapper>(
       *dstBufPtr,
       dstBufSize,
       dstBufAlignment,
       (F const&)f
      );
     } else {
      using NonAllocWrapper = NonAllocLambdaWrapper<F, Prototype>;

      //Check before copying NonAllocLambdaWrapper to Lambda buffer
      wrapperBufferCheck(
       sizeof(NonAllocWrapper),
       alignof(NonAllocWrapper),
       dstBufSize,
       dstBufAlignment
      );

      //Initialize non-allocated buffer with NonAllocLambdaWrapper
      //and buffer properties
      LambdaOperationBase<
       Prototype,
       Restriction,
       false
      >::template copyInitWrapper<NonAllocWrapper>(
       *dstBufPtr,
       dstBufSize,
       dstBufAlignment,
       (F const&)f
      );
     }
    }

    [[gnu::always_inline]]
    bool present() const noexcept override {
     return true;
    }
   };
  #endif //CX_STL_SUPPORT
 }

 //Unqualified lambda specialization
 template<typename R, typename... Args>
 struct Lambda<R (Args...)> {
  template<typename>
  friend struct Lambda;

  template<typename>
  friend struct AllocLambda;

  template<
   typename,
   template<typename> typename,
   bool
  >
  friend struct Internal::LambdaOperationBase;

  using ReturnType = R;
  template<template<typename...> typename Receiver = Dummy>
  using ArgumentTypes = Receiver<Args...>;
  using FunctionType = R (Args...);

  static constexpr auto const Alignment = CX_LAMBDA_BUF_ALIGN;
  static constexpr auto const Size = CX_LAMBDA_BUF_SIZE;

 private:
  template<typename F>
  requires (FunctionWithPrototype<F, R (Args...)>
   || FunctionWithPrototype<F, R (Args...) noexcept>
   || FunctionWithPrototype<F, R (F::*)(Args...)>
   || FunctionWithPrototype<F, R (F::*)(Args...) const>
   || FunctionWithPrototype<F, R (F::*)(Args...) noexcept>
   || FunctionWithPrototype<F, R (F::*)(Args...) const noexcept>
  )
  struct PrototypeRestriction {};

  template<typename F>
  static constexpr bool const SupportedPrototype = Internal::SupportedPrototype<
   F,
   PrototypeRestriction
  >;

  using OperationBase = Internal::LambdaOperationBase<
   R (Args...),
   PrototypeRestriction,
   false
  >;
  using LambdaBase = Internal::LambdaBase<R (Args...)>;

  alignas(Alignment) unsigned char buffer[Size];

  //Return mutable reference to `buffer`
  auto& buf() const noexcept {
   return const_cast<Lambda&>(*this).buffer;
  }

 public:
  //Default constructor
  Lambda() {
   //Default initialize buffer for empty lambda
   OperationBase::initEmptyLambda(&buf(), Size, Alignment);
  }

  //FunctionOperator / function pointer copy constructor
  template<typename F>
  requires (!IsLambda<F> && SupportedPrototype<F>)
  Lambda(F const &f) : Lambda() {
   operator=<F>((F const&)f);
  }

  //FunctionOperator move constructor
  template<typename F>
  requires (!IsLambda<F>
   && !Const<F>
   && Struct<F>
   && SupportedPrototype<F>
  )
  Lambda(F &&f) : Lambda() {
   operator=<F>((F&&)f);
  }

  //Lambda copy constructor
  Lambda(Lambda const &l) : Lambda() {
   operator=<Lambda>((Lambda const&)l);
  }

  //Lambda move constructor
  Lambda(Lambda &&l) : Lambda() {
   operator=<Lambda>((Lambda&&)l);
  }

  //CompatibleLambda copy constructor
  template<CompatibleLambda<Lambda> L>
  Lambda(L const &l) : Lambda() {
   operator=<L>((L const&)l);
  }

  //CompatibleLambda move constructor
  template<CompatibleLambda<Lambda> L>
  requires (!Const<L>)
  Lambda(L &&l) : Lambda() {
   operator=<L>((L&&)l);
  }

  ~Lambda() {
   //Destruct current buffer (guaranteed to be initialized)
   OperationBase::destroy(&buf());
  }

  //Lambda function operator
  R operator()(Args... args) const {
   return (*(LambdaBase *)&buffer).op(args...);
  }

  //Presence conversion operator
  operator bool() const noexcept {
   return (*(LambdaBase *)&buffer).present();
  }

  //Implicit lambda conversion operator
  template<CompatibleLambda<Lambda> L>
  explicit operator L() const {
   return L{*this};
  }

  //FunctionOperator / function pointer copy assignment
  template<typename F>
  requires (!IsLambda<F> && SupportedPrototype<F>)
  Lambda& operator=(F const &f) {
   //Ensure `F` will fit within `buffer`
   OperationBase::template lambdaAssignBufferCheck<F, Size, Alignment>();

   //Initialize buffer
   OperationBase::copyAssignFunction(*this, (F const&)f);

   return *this;
  }

  //FunctionOperator move assignment
  template<typename F>
  requires (!IsLambda<F>
   && !Const<F>
   && Struct<F>
   && SupportedPrototype<F>
  )
  Lambda& operator=(F &&f) {
   //Ensure `F` will fit within `buffer`
   OperationBase::template lambdaAssignBufferCheck<F, Size, Alignment>();

   //Initialize buffer
   OperationBase::moveAssignFunction(*this, (F&&)f);

   return *this;
  }

  //Lambda copy assignment
  Lambda& operator=(Lambda const &l) {
   return operator=<Lambda>((Lambda const&)l);
  }

  //Lambda move assignment
  Lambda& operator=(Lambda &&l) {
   return operator=<Lambda>((Lambda&&)l);
  }

  //CompatibleLambda copy assignment
  template<CompatibleLambda<Lambda> L>
  Lambda& operator=(L const &l) {
   OperationBase::copyAssignLambda(*this, l);
   return *this;
  }

  //CompatibleLambda move assignment
  template<CompatibleLambda<Lambda> L>
  requires (!Const<L>)
  Lambda& operator=(L &&l) {
   OperationBase::moveAssignLambda(*this, l);
   return *this;
  }
 };

 //Noexcept qualified lambda specialization
 template<typename R, typename... Args>
 struct Lambda<R (Args...) noexcept> {
  template<typename>
  friend struct Lambda;

  template<typename>
  friend struct AllocLambda;

  template<
   typename,
   template<typename> typename,
   bool
  >
  friend struct Internal::LambdaOperationBase;

  using ReturnType = R;
  template<template<typename...> typename Receiver = Dummy>
  using ArgumentTypes = Receiver<Args...>;
  using FunctionType = R (Args...) noexcept;

  static constexpr auto const Alignment = CX_LAMBDA_BUF_ALIGN;
  static constexpr auto const Size = CX_LAMBDA_BUF_SIZE;

 private:
  template<typename F>
  requires (FunctionWithPrototype<F, R (Args...) noexcept>
   || FunctionWithPrototype<F, R (F::*)(Args...) noexcept>
   || FunctionWithPrototype<F, R (F::*)(Args...) const noexcept>
  )
  struct PrototypeRestriction {};

  template<typename F>
  static constexpr bool const SupportedPrototype = Internal::SupportedPrototype<
   F,
   PrototypeRestriction
  >;

  using OperationBase = Internal::LambdaOperationBase<
   R (Args...),
   PrototypeRestriction,
   false
  >;
  using LambdaBase = Internal::LambdaBase<R (Args...)>;

  alignas(Alignment) unsigned char buffer[Size];

  //Return mutable reference to `buffer`
  auto& buf() const noexcept {
   return const_cast<Lambda&>(*this).buffer;
  }

 public:
  //Default constructor
  Lambda() {
   //Default initialize buffer for empty lambda
   OperationBase::initEmptyLambda(&buf(), Size, Alignment);
  }

  //FunctionOperator / function pointer copy constructor
  template<typename F>
  requires (!IsLambda<F> && SupportedPrototype<F>)
  Lambda(F const &f) : Lambda() {
   operator=<F>((F const&)f);
  }

  //FunctionOperator move constructor
  template<typename F>
  requires (!IsLambda<F>
   && !Const<F>
   && Struct<F>
   && SupportedPrototype<F>
  )
  Lambda(F &&f) : Lambda() {
   operator=<F>((F&&)f);
  }

  //Lambda copy constructor
  Lambda(Lambda const &l) : Lambda() {
   operator=<Lambda>((Lambda const&)l);
  }

  //Lambda move constructor
  Lambda(Lambda &&l) : Lambda() {
   operator=<Lambda>((Lambda&&)l);
  }

  //CompatibleLambda copy constructor
  template<CompatibleLambda<Lambda> L>
  Lambda(L const &l) : Lambda() {
   operator=<L>((L const&)l);
  }

  //CompatibleLambda move constructor
  template<CompatibleLambda<Lambda> L>
  requires (!Const<L>)
  Lambda(L &&l) : Lambda() {
   operator=<L>((L&&)l);
  }

  ~Lambda() {
   //Destruct current buffer (guaranteed to be initialized)
   OperationBase::destroy(&buf());
  }

  //Lambda function operator
  R operator()(Args... args) noexcept {
   return (*(LambdaBase *)&buffer).noexceptOp(args...);
  }

  //Presence conversion operator
  operator bool() const noexcept {
   return (*(LambdaBase *)&buffer).present();
  }

  //Implicit lambda conversion operator
  template<CompatibleLambda<Lambda> L>
  explicit operator L() const {
   return {*this};
  }

  //FunctionOperator / function pointer copy assignment
  template<typename F>
  requires (!IsLambda<F> && SupportedPrototype<F>)
  Lambda& operator=(F const &f) {
   //Ensure `F` will fit within `buffer`
   OperationBase::template lambdaAssignBufferCheck<F, Size, Alignment>();

   //Initialize buffer
   OperationBase::copyAssignFunction(*this, (F const&)f);

   return *this;
  }

  //FunctionOperator move assignment
  template<typename F>
  requires (!IsLambda<F>
   && !Const<F>
   && Struct<F>
   && SupportedPrototype<F>
  )
  Lambda& operator=(F &&f) {
   //Ensure `F` will fit within `buffer`
   OperationBase::template lambdaAssignBufferCheck<F, Size, Alignment>();

   //Initialize buffer
   OperationBase::moveAssignFunction(*this, (F&&)f);

   return *this;
  }

  //Lambda copy assignment
  Lambda& operator=(Lambda const &l) {
   return operator=<Lambda>((Lambda const&)l);
  }

  //Lambda move assignment
  Lambda& operator=(Lambda &&l) {
   return operator=<Lambda>((Lambda&&)l);
  }

  //CompatibleLambda copy assignment
  template<CompatibleLambda<Lambda> L>
  Lambda& operator=(L const &l) {
   OperationBase::copyAssignLambda(*this, l);
   return *this;
  }

  //CompatibleLambda move assignment
  template<CompatibleLambda<Lambda> L>
  requires (!Const<L>)
  Lambda& operator=(L &&l) {
   OperationBase::moveAssignLambda(*this, l);
   return *this;
  }
 };

 //Unqualified c-variadic lambda specialization
 template<typename R, typename... Args>
 struct Lambda<R (Args..., ...)> {
  template<typename>
  friend struct Lambda;

  template<typename>
  friend struct AllocLambda;

  template<
   typename,
   template<typename> typename,
   bool
  >
  friend struct Internal::LambdaOperationBase;

  using ReturnType = R;
  template<template<typename...> typename Receiver = Dummy>
  using ArgumentTypes = Receiver<Args...>;
  using FunctionType = R (Args..., ...);

  static constexpr auto const Alignment = CX_LAMBDA_BUF_ALIGN;
  static constexpr auto const Size = CX_LAMBDA_BUF_SIZE;

 private:
  template<typename F>
  requires (FunctionWithPrototype<F, R (Args..., ...)>
   || FunctionWithPrototype<F, R (Args..., ...) noexcept>
   || FunctionWithPrototype<F, R (F::*)(Args..., ...)>
   || FunctionWithPrototype<F, R (F::*)(Args..., ...) const>
   || FunctionWithPrototype<F, R (F::*)(Args..., ...) noexcept>
   || FunctionWithPrototype<F, R (F::*)(Args..., ...) const noexcept>
  )
  struct PrototypeRestriction {};

  template<typename F>
  static constexpr bool const SupportedPrototype = Internal::SupportedPrototype<
   F,
   PrototypeRestriction
  >;

  using OperationBase = Internal::LambdaOperationBase<
   R (Args..., ...),
   PrototypeRestriction,
   false
  >;
  using LambdaBase = Internal::LambdaBase<R (Args..., ...)>;

  alignas(Alignment) unsigned char buffer[Size];

  //Return mutable reference to `buffer`
  auto& buf() const noexcept {
   return const_cast<Lambda&>(*this).buffer;
  }

 public:
  //Default constructor
  Lambda() {
   //Default initialize buffer for empty lambda
   OperationBase::initEmptyLambda(&buf(), Size, Alignment);
  }

  //FunctionOperator / function pointer copy constructor
  template<typename F>
  requires (!IsLambda<F> && SupportedPrototype<F>)
  Lambda(F const &f) : Lambda() {
   operator=<F>((F const&)f);
  }

  //FunctionOperator move constructor
  template<typename F>
  requires (!IsLambda<F>
   && !Const<F>
   && Struct<F>
   && SupportedPrototype<F>
  )
  Lambda(F &&f) : Lambda() {
   operator=<F>((F &&)f);
  }

  //Lambda copy constructor
  Lambda(Lambda const &l) : Lambda() {
   operator=<Lambda>((Lambda const&)l);
  }

  //Lambda move constructor
  Lambda(Lambda &&l) : Lambda() {
   operator=<Lambda>((Lambda&&)l);
  }

  //CompatibleLambda copy constructor
  template<CompatibleLambda<Lambda> L>
  Lambda(L const &l) : Lambda() {
   operator=<L>((L const&)l);
  }

  //CompatibleLambda move constructor
  template<CompatibleLambda<Lambda> L>
  requires (!Const<L>)
  Lambda(L &&l) : Lambda() {
   operator=<L>((L&&)l);
  }

  ~Lambda() {
   //Destruct current buffer (guaranteed to be initialized)
   OperationBase::destroy(buf());
  }

  //Lambda function operator
  template<typename... Varargs>
  R operator()(Args... args, Varargs... varargs) {
   return (*(LambdaBase *)&buffer).template op<false>(args..., varargs...);
  }

  //Presence conversion operator
  operator bool() const noexcept {
   return (*(LambdaBase *)&buffer).present();
  }

  //Implicit lambda conversion operator
  template<CompatibleLambda<Lambda> L>
  explicit operator L() const {
   L{*this};
  }

  //FunctionOperator / function pointer copy assignment
  template<typename F>
  requires (!IsLambda<F> && SupportedPrototype<F>)
  Lambda& operator=(F const &f) {
   //Ensure `F` will fit within `buffer`
   OperationBase::template lambdaAssignBufferCheck<F, Size, Alignment>();

   //Initialize buffer
   OperationBase::copyAssignFunction(*this, (F const&)f);

   return *this;
  }

  //FunctionOperator move assignment
  template<typename F>
  requires (!IsLambda<F>
   && !Const<F>
   && Struct<F>
   && SupportedPrototype<F>
  )
  Lambda& operator=(F &&f) {
   //Ensure `F` will fit within `buffer`
   OperationBase::template lambdaAssignBufferCheck<F, Size, Alignment>();

   //Initialize buffer
   OperationBase::moveAssignFunction(*this, (F&&)f);

   return *this;
  }

  //Lambda copy assignment
  Lambda& operator=(Lambda const &l) {
   return operator=<Lambda>((Lambda const&)l);
  }

  //Lambda move assignment
  Lambda& operator=(Lambda &&l) {
   return operator=<Lambda>((Lambda&&)l);
  }

  //CompatibleLambda copy assignment
  template<CompatibleLambda<Lambda> L>
  Lambda& operator=(L const &l) {
   OperationBase::copyAssignLambda(*this, l);
   return *this;
  }

  //CompatibleLambda move assignment
  template<CompatibleLambda<Lambda> L>
  requires (!Const<L>)
  Lambda& operator=(L &&l) {
   OperationBase::moveAssignLambda(*this, l);
   return *this;
  }
 };

 //Noexcept qualified c-variadic lambda specialization
 template<typename R, typename... Args>
 struct Lambda<R (Args..., ...) noexcept> {
  template<typename>
  friend struct Lambda;

  template<typename>
  friend struct AllocLambda;

  template<
   typename,
   template<typename> typename,
   bool
  >
  friend struct Internal::LambdaOperationBase;

  using ReturnType = R;
  template<template<typename...> typename Receiver = Dummy>
  using ArgumentTypes = Receiver<Args...>;
  using FunctionType = R (Args..., ...) noexcept;

  static constexpr auto const Alignment = CX_LAMBDA_BUF_ALIGN;
  static constexpr auto const Size = CX_LAMBDA_BUF_SIZE;

 private:
  template<typename F>
  requires (FunctionWithPrototype<F, R (Args..., ...) noexcept>
   || FunctionWithPrototype<F, R (F::*)(Args..., ...) noexcept>
   || FunctionWithPrototype<F, R (F::*)(Args..., ...) const noexcept>
  )
  struct PrototypeRestriction {};

  template<typename F>
  static constexpr bool const SupportedPrototype = Internal::SupportedPrototype<
   F,
   PrototypeRestriction
  >;

  using OperationBase = Internal::LambdaOperationBase<
   R (Args..., ...),
   PrototypeRestriction,
   false
  >;
  using LambdaBase = Internal::LambdaBase<R (Args..., ...)>;

  alignas(Alignment) unsigned char buffer[Size];

  //Return mutable reference to `buffer`
  auto& buf() const noexcept {
   return const_cast<Lambda&>(*this).buffer;
  }

 public:
  //Default constructor
  Lambda() {
   //Default initialize buffer for empty lambda
   OperationBase::initEmptyLambda(&buf(), Size, Alignment);
  }

  //FunctionOperator / function pointer copy constructor
  template<typename F>
  requires (!IsLambda<F> && SupportedPrototype<F>)
  Lambda(F const &f) : Lambda() {
   operator=<F>((F const&)f);
  }

  //FunctionOperator move constructor
  template<typename F>
  requires (!IsLambda<F>
   && !Const<F>
   && Struct<F>
   && SupportedPrototype<F>
  )
  Lambda(F &&f) : Lambda() {
   operator=<F>((F&&)f);
  }

  //Lambda copy constructor
  Lambda(Lambda const &l) : Lambda() {
   operator=<Lambda>((Lambda const&)l);
  }

  //Lambda move constructor
  Lambda(Lambda &&l) : Lambda() {
   operator=<Lambda>((Lambda&&)l);
  }

  //CompatibleLambda copy constructor
  template<CompatibleLambda<Lambda> L>
  Lambda(L const &l) : Lambda() {
   operator=<L>((L const &)l);
  }

  //CompatibleLambda move constructor
  template<CompatibleLambda<Lambda> L>
  requires (!Const<L>)
  Lambda(L &&l) {
   operator=<L>((L&&)l);
  }

  ~Lambda() {
   //Destruct current buffer (guaranteed to be initialized)
   OperationBase::destroy(buf());
  }

  //Lambda function operator
  template<typename... Varargs>
  R operator()(Args... args, Varargs... varargs) {
   return (*(LambdaBase *)&buffer).template op<true>(args..., varargs...);
  }

  //Presence conversion operator
  operator bool() const noexcept {
   return (*(LambdaBase *)&buffer).present();
  }

  //Explicit lambda conversion operator
  template<CompatibleLambda<Lambda> L>
  explicit operator L() const {
   return L{*this};
  }

  //FunctionOperator / function pointer copy assignment
  template<typename F>
  requires (!IsLambda<F> && SupportedPrototype<F>)
  Lambda& operator=(F const &f) {
   //Ensure `F` will fit within `buffer`
   OperationBase::template lambdaAssignBufferCheck<F, Size, Alignment>();

   //Initialize buffer
   OperationBase::copyAssignFunction(*this, (F const&)f);

   return *this;
  }

  //FunctionOperator move assignment
  template<typename F>
  requires (!IsLambda<F>
   && !Const<F>
   && Struct<F>
   && SupportedPrototype<F>
  )
  Lambda& operator=(F &&f) {
   //Ensure `F` will fit within `buffer`
   OperationBase::template lambdaAssignBufferCheck<F, Size, Alignment>();

   //Initialize buffer
   OperationBase::moveAssignFunction(*this, (F&&)f);

   return *this;
  }

  //Lambda copy assignment
  Lambda& operator=(Lambda const &l) {
   return operator=<Lambda>((Lambda const&)l);
  }

  //Lambda move assignment
  Lambda& operator=(Lambda &&l) {
   return operator=<Lambda>((Lambda&&)l);
  }

  //CompatibleLambda copy assignment
  template<CompatibleLambda<Lambda> L>
  Lambda& operator=(L const &l) {
   OperationBase::copyAssignLambda(*this, l);
   return *this;
  }

  //CompatibleLambda move assignment
  template<CompatibleLambda<Lambda> L>
  requires (!Const<L>)
  Lambda& operator=(L &&l) {
   OperationBase::moveAssignLambda(*this, l);
   return *this;
  }
 };

 //Deduction guides for Lambda
 Lambda() -> Lambda<void ()>;

 template<StaticFunction F>
 Lambda(F const&) -> Lambda<FunctionPrototype<F>>;

 template<typename F>
 requires (!IsLambda<F> && FunctionOperator<F, decltype(&F::operator())>)
 Lambda(F const&) -> Lambda<FunctionPrototype<decltype(&F::operator())>>;

 template<typename F>
 requires (StaticFunction<F>)
 Lambda(F&&) -> Lambda<FunctionPrototype<F>>;

 template<typename F>
 requires (!IsLambda<F> && FunctionOperator<F, decltype(&F::operator())>)
 Lambda(F&&) -> Lambda<FunctionPrototype<decltype(&F::operator())>>;

 template<typename L>
 requires (IsLambda<L>)
 Lambda(L const&) -> Lambda<typename Unqualified<L>::FunctionType>;

 template<typename L>
 requires (IsLambda<L>)
 Lambda(L&&) -> Lambda<typename Unqualified<L>::FunctionType>;

 //AllocLambda implementation
 #ifdef CX_STL_SUPPORT
  //Unqualified lambda specialization
  template<typename R, typename... Args>
  struct AllocLambda<R (Args...)> {
   template<typename>
   friend struct Lambda;

   template<typename>
   friend struct AllocLambda;

   template<
    typename,
    template<typename> typename,
    bool
   >
   friend struct Internal::LambdaOperationBase;

   using ReturnType = R;
   template<template<typename...> typename Receiver = Dummy>
   using ArgumentTypes = Receiver<Args...>;
   using FunctionType = R (Args...);

  private:
   //Note: Reuse prototype restrictions from `Lambda<R (Args...)>`
   template<typename F>
   using PrototypeRestriction = typename Lambda<R (Args...)>::template PrototypeRestriction<F>;

   template<typename F>
   static constexpr bool const SupportedPrototype = Internal::SupportedPrototype<
    R (Args...),
    PrototypeRestriction
   >;

   using OperationBase = Internal::LambdaOperationBase<
    R (Args...),
    PrototypeRestriction,
    true
   >;
   using LambdaBase = Internal::LambdaBase<R (Args...)>;
   static constexpr auto const Deleter = &OperationBase::sharedBufferDeleter;

   std::shared_ptr<unsigned char> mutable buffer;

  public:
   //Default constructor
   AllocLambda() {
    OperationBase::allocLambdaBufferOp(buffer, [](void ** bufPtr) {
     OperationBase::initEmptyLambda(
      bufPtr,
      0,
      0
     );
    });
   }

   //FunctionOperator / function pointer copy constructor
   template<typename F>
   requires (!IsLambda<F> && SupportedPrototype<F>)
   AllocLambda(F const &f) : AllocLambda() {
    operator=<F>((F const&)f);
   }

   //FunctionOperator move constructor
   template<typename F>
   requires (!IsLambda<F>
    && !Const<F>
    && Struct<F>
    && SupportedPrototype<F>
   )
   AllocLambda(F &&f) : AllocLambda() {
    operator=<F>((F&&)f);
   }

   //Lambda copy constructor
   AllocLambda(AllocLambda const &l) : AllocLambda() {
    operator=<AllocLambda>((AllocLambda const&)l);
   }

   //Lambda move constructor
   AllocLambda(AllocLambda &&l) : AllocLambda() {
    operator=<AllocLambda>((AllocLambda&&)l);
   }

   //CompatibleLambda copy constructor
   template<CompatibleLambda<AllocLambda> L>
   AllocLambda(L const &l) : AllocLambda() {
    operator=<L>((L const&)l);
   }

   //CompatibleLambda move constructor
   template<CompatibleLambda<AllocLambda> L>
   requires (!Const<L>)
   AllocLambda(L &&l) : AllocLambda() {
    operator=<L>((L&&)l);
   }

   ~AllocLambda() = default;

   //Lambda function operator
   R operator()(Args... args) const {
    return (*(LambdaBase *)buffer.get()).op(args...);
   }

   //Presence conversion operator
   operator bool() const noexcept {
    return (*(LambdaBase *)buffer.get()).present();
   }

   //Implicit lambda conversion operator
   template<CompatibleLambda<AllocLambda> L>
   explicit operator L() const {
    return L{*this};
   }

   //FunctionOperator / function pointer copy assignment
   template<typename F>
   requires (!IsLambda<F> && SupportedPrototype<F>)
   AllocLambda& operator=(F const &f) {
    //Initialize buffer
    OperationBase::copyAssignFunction(*this, (F const &)f);

    return *this;
   }

   //FunctionOperator move assignment
   template<typename F>
   requires (!IsLambda<F>
    && !Const<F>
    && Struct<F>
    && SupportedPrototype<F>
   )
   AllocLambda& operator=(F &&f) {
    //Initialize buffer
    OperationBase::moveAssignFunction(*this, (F&&)f);

    return *this;
   }

   //Lambda copy assignment
   AllocLambda& operator=(AllocLambda const &l) {
    return operator=<AllocLambda>((AllocLambda const&)l);
   }

   //Lambda move assignment
   AllocLambda& operator=(AllocLambda &&l) {
    return operator=<AllocLambda>((AllocLambda&&)l);
   }

   //CompatibleLambda copy assignment
   template<CompatibleLambda<AllocLambda> L>
   AllocLambda& operator=(L const &l) {
    OperationBase::copyAssignLambda(*this, l);
    return *this;
   }

   //CompatibleLambda move assignment
   template<CompatibleLambda<AllocLambda> L>
   requires (!Const<L>)
   AllocLambda& operator=(L &&l) {
    OperationBase::moveAssignLambda(*this, l);
    return *this;
   }
  };

  //TODO Noexcept qualified lambda specialization
  template<typename R, typename... Args>
  struct AllocLambda<R (Args...) noexcept > {};

  //TODO Unqualified c-variadic lambda specialization
  template<typename R, typename... Args>
  struct AllocLambda<R (Args..., ...)> {};

  //TODO Noexcept qualified c-variadic lambda specialization
  template<typename R, typename... Args>
  struct AllocLambda<R (Args..., ...) noexcept> {};
 #endif //CX_STL_SUPPORT

 //Deduction guides for AllocLambda
 AllocLambda() -> AllocLambda<void ()>;

 template<typename F>
 requires (StaticFunction<F>)
 AllocLambda(F const&) -> AllocLambda<FunctionPrototype<F>>;

 template<typename F>
 requires (!IsLambda<F> && FunctionOperator<F, decltype(&F::operator())>)
 AllocLambda(F const&) -> AllocLambda<FunctionPrototype<decltype(&F::operator())>>;

 template<typename F>
 requires (StaticFunction<F>)
 AllocLambda(F&&) -> AllocLambda<FunctionPrototype<F>>;

 template<typename F>
 requires (!IsLambda<F> && FunctionOperator<F, decltype(&F::operator())>)
 AllocLambda(F&&) -> AllocLambda<FunctionPrototype<decltype(&F::operator())>>;

 template<typename L>
 requires (IsLambda<L>)
 AllocLambda(L const&) -> AllocLambda<typename Unqualified<L>::FunctionType>;

 template<typename L>
 requires (IsLambda<L>)
 AllocLambda(L&&) -> AllocLambda<typename Unqualified<L>::FunctionType>;

 //TODO conversion from c-variadic lambda to function pointer
 //Note: Use `CX::VaList` as the prototype requirement instead
}

//Clean up internal macros
#undef CX_STL_SUPPORT_EXPR
