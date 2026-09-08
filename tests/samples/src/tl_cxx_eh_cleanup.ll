; Fixture PE32+ mínima para validar stateUnwindMap/cleanupret na ABI MSVC.
; O destrutor marca a execução no unwind; o catch só termina com 0 se a
; limpeza ocorreu antes da captura.
target triple = "x86_64-pc-windows-msvc"

@cxx_parameters = private constant [3 x i64] zeroinitializer
@cleanup_marker = internal global i32 0
declare dllimport void @RaiseException(i32, i32, i32, ptr)
declare dllimport void @ExitProcess(i32)
declare i32 @__CxxFrameHandler3(...)

define void @tl_destructor() {
entry:
  store i32 1, ptr @cleanup_marker
  ret void
}

define void @tl_entry() personality ptr @__CxxFrameHandler3 {
entry:
  invoke void @RaiseException(i32 3765269347, i32 0, i32 3, ptr @cxx_parameters)
          to label %normal unwind label %cleanup
normal:
  call void @ExitProcess(i32 3)
  unreachable
cleanup:
  %cleanup_pad = cleanuppad within none []
  call void @tl_destructor() [ "funclet"(token %cleanup_pad) ]
  cleanupret from %cleanup_pad unwind label %lpad_catch
lpad_catch:
  %catch_switch = catchswitch within none [label %catch] unwind label %lpad_terminate
catch:
  %catch_pad = catchpad within %catch_switch [ptr null, i32 64, ptr null]
  catchret from %catch_pad to label %caught
lpad_terminate:
  %terminate_pad = cleanuppad within none []
  call void @ExitProcess(i32 2)
  unreachable
caught:
  %marker = load i32, ptr @cleanup_marker
  %cleaned = icmp eq i32 %marker, 1
  br i1 %cleaned, label %success, label %failure
success:
  call void @ExitProcess(i32 0)
  unreachable
failure:
  call void @ExitProcess(i32 1)
  unreachable
}
