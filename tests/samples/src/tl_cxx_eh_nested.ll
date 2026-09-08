; Fixture PE32+ para validar uma exceção lançada dentro de um catch funclet.
; A sequência exercita o encadeamento de catchswitch/catchpad do WinEH.
target triple = "x86_64-pc-windows-msvc"

@type_descriptor = private constant { ptr, ptr, [16 x i8] } {
  ptr null, ptr null, [16 x i8] c".?AVTL_NESTED@@\00"
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
@nested_marker = internal global i32 0
declare dllimport void @RaiseException(i32, i32, i32, ptr)
declare dllimport void @ExitProcess(i32)
declare i32 @__CxxFrameHandler3(...)

define void @tl_mark_outer() {
entry:
  store i32 1, ptr @nested_marker
  ret void
}

define void @tl_mark_inner() {
entry:
  store i32 2, ptr @nested_marker
  ret void
}

define void @tl_entry() personality ptr @__CxxFrameHandler3 {
entry:
  invoke void @RaiseException(i32 3765269347, i32 0, i32 3, ptr @cxx_parameters)
          to label %normal unwind label %outer_dispatch
normal:
  call void @ExitProcess(i32 3)
  unreachable
outer_dispatch:
  %outer_switch = catchswitch within none [label %outer_catch] unwind label %terminate
outer_catch:
  %outer_pad = catchpad within %outer_switch [ptr @type_descriptor, i32 0, ptr null]
  call void @tl_mark_outer() [ "funclet"(token %outer_pad) ]
  invoke void @RaiseException(i32 3765269347, i32 0, i32 3, ptr @cxx_parameters)
          [ "funclet"(token %outer_pad) ] to label %inner_cont unwind label %inner_dispatch
inner_dispatch:
  %inner_switch = catchswitch within %outer_pad [label %inner_catch] unwind to caller
inner_catch:
  %inner_pad = catchpad within %inner_switch [ptr @type_descriptor, i32 0, ptr null]
  call void @tl_mark_inner() [ "funclet"(token %inner_pad) ]
  catchret from %inner_pad to label %inner_cont
inner_cont:
  catchret from %outer_pad to label %caught
terminate:
  %terminate_pad = cleanuppad within none []
  call void @ExitProcess(i32 2) [ "funclet"(token %terminate_pad) ]
  unreachable
caught:
  %marker = load i32, ptr @nested_marker
  %matched = icmp eq i32 %marker, 2
  br i1 %matched, label %success, label %failure
success:
  call void @ExitProcess(i32 0)
  unreachable
failure:
  call void @ExitProcess(i32 1)
  unreachable
}
