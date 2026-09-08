; Fixture PE32+ para validar duas ações consecutivas de stateUnwindMap.
; Os dois marcadores precisam estar presentes antes do catch continuar.
target triple = "x86_64-pc-windows-msvc"

@cxx_parameters = private constant [3 x i64] zeroinitializer
@cleanup_marker = internal global i32 0
declare dllimport void @RaiseException(i32, i32, i32, ptr)
declare dllimport void @ExitProcess(i32)
declare i32 @__CxxFrameHandler3(...)

define void @tl_outer_destructor() {
entry:
  %old = load i32, ptr @cleanup_marker
  %new = or i32 %old, 1
  store i32 %new, ptr @cleanup_marker
  ret void
}

define void @tl_inner_destructor() {
entry:
  %old = load i32, ptr @cleanup_marker
  %new = or i32 %old, 2
  store i32 %new, ptr @cleanup_marker
  ret void
}

define void @tl_entry() personality ptr @__CxxFrameHandler3 {
entry:
  invoke void @RaiseException(i32 3765269347, i32 0, i32 3, ptr @cxx_parameters)
          to label %normal unwind label %cleanup_outer
normal:
  call void @ExitProcess(i32 3)
  unreachable
cleanup_outer:
  %outer_pad = cleanuppad within none []
  call void @tl_outer_destructor() [ "funclet"(token %outer_pad) ]
  cleanupret from %outer_pad unwind label %cleanup_inner
cleanup_inner:
  %inner_pad = cleanuppad within none []
  call void @tl_inner_destructor() [ "funclet"(token %inner_pad) ]
  cleanupret from %inner_pad unwind label %lpad_catch
lpad_catch:
  %catch_switch = catchswitch within none [label %catch] unwind label %lpad_terminate
catch:
  %catch_pad = catchpad within %catch_switch [ptr null, i32 64, ptr null]
  catchret from %catch_pad to label %caught
lpad_terminate:
  %terminate_pad = cleanuppad within none []
  call void @ExitProcess(i32 2) [ "funclet"(token %terminate_pad) ]
  unreachable
caught:
  %marker = load i32, ptr @cleanup_marker
  %cleaned = icmp eq i32 %marker, 3
  br i1 %cleaned, label %success, label %failure
success:
  call void @ExitProcess(i32 0)
  unreachable
failure:
  call void @ExitProcess(i32 1)
  unreachable
}
