
* Recursive function call on device.  This is very important for ECP
  WarpX code.

* Classes that are not standard layout.  The current specification of
  oneAPI does not support the capture of objects that are not standard
  layout.  This includes the following example,

  ```
  class A {int a;}; class B {long B;}; class C : A, B {};
  ```

  AMReX has a data structure called GpuTuple that is built with a
  pattern like the example shown above.  It works in CUDA, but not in
  DPC++.  We wish this requirement can be relaxed.

* Host callback.  Could DPC++ support appending a host callback
  function to an ordered queue?

* Global variables.  Could DPC++ support global variables and add
  something similar to cudaMemcpyToSymbol?

* Option to be less OOP.  Could we have access to thread id, group id,
  memory fence, barrier functions, etc. without using an nd_item like
  object?

* Local memory.  Could DPC++ support static local memory
  (e.g. something like CUDA `__shared__ a[256]`) and dynamic local
  memory (e.g., something like CUDA `extern __shared__ a[]` with the
  amount of memory specified at runtime during kernel launch) from
  anywhere in device code?

* Compiler flag to make implicit capture of this pointer via `[=]` an
  error.  [Implicit capture of this pointer]
  (http://eel.is/c++draft/depr#capture.this) has been deprecated in
  C++ 20.  For many codes, it's almost always a bug when `this` is
  implicitly captured via `[=]`.

* assert(0). assert(0) when called on device does not throw any errors
  or abort the run.  Is it possible to make it abort?

* sycl::abs. sycl::abs(int) returns an unsigned int in contrast to int
  std::abs(int).  Currently std::abs does not work on device.  If
  std::abs is made to work on device, could we make sure it has the
  signature of `int std::abs(in)`?

* Memory fence.  Could DPC++ privode a memory fence function for the
  whole device (not just group)?  Or is the CUDA distinction between
  `__threadfence` and `__thread_block` unnecessary for Intel GPUs?