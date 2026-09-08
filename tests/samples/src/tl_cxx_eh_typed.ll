; Fixture PE32+ mínima para validar a seleção de catch tipado na ABI MSVC.
; As estruturas ThrowInfo/CatchableType usam RVAs relativos à imagem.
target triple = "x86_64-pc-windows-msvc"

@type_descriptor = private constant { ptr, ptr, [15 x i8] } {
  ptr null, ptr null, [15 x i8] c".?AVTL_TYPED@@\00"
}
@catchable_type = private constant [7 x i32] [
  i32 0,
  i32 sub (i32 ptrtoint (ptr @type_descriptor to i32), i32 268435456),
  i32 0,
  i32 0,
  i32 0,
  i32 0,
  i32 0
]
@catchable_type_array = private constant [2 x i32] [
  i32 1,
  i32 sub (i32 ptrtoint (ptr @catchable_type to i32), i32 268435456)
]
@throw_info = private constant [4 x i32] [
  i32 0,
  i32 0,
  i32 0,
  i32 sub (i32 ptrtoint (ptr @catchable_type_array to i32), i32 268435456)
]
@cxx_parameters = private constant [3 x i64] [
  i64 0,
  i64 0,
  i64 ptrtoint (ptr @throw_info to i64)
]
@typed_marker = internal global i32 0
declare dllimport void @RaiseException(i32, i32, i32, ptr)
declare dllimport void @ExitProcess(i32)
declare i32 @__CxxFrameHandler3(...)

define void @tl_mark() {
entry:
  store i32 1, ptr @typed_marker
  ret void
}

define void @tl_entry() personality ptr @__CxxFrameHandler3 {
entry:
  invoke void @RaiseException(i32 3765269347, i32 0, i32 3, ptr @cxx_parameters)
          to label %normal unwind label %lpad_catch
normal:
  call void @ExitProcess(i32 3)
  unreachable
lpad_catch:
  %catch_switch = catchswitch within none [label %catch_typed] unwind label %lpad_terminate
catch_typed:
  %catch_pad = catchpad within %catch_switch [ptr @type_descriptor, i32 0, ptr null]
  call void @tl_mark() [ "funclet"(token %catch_pad) ]
  catchret from %catch_pad to label %caught
lpad_terminate:
  %terminate_pad = cleanuppad within none []
  call void @ExitProcess(i32 2) [ "funclet"(token %terminate_pad) ]
  unreachable
caught:
  %marker = load i32, ptr @typed_marker
  %matched = icmp eq i32 %marker, 1
  br i1 %matched, label %success, label %failure
success:
  call void @ExitProcess(i32 0)
  unreachable
failure:
  call void @ExitProcess(i32 1)
  unreachable
}
