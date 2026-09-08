; Fixture PE32+ mínima com a representação LLVM/WinEH da ABI MSVC.
; Ela só será usada para validar .pdata/.xdata e o contrato do handler;
; nenhum código de aplicativo real é incorporado.
target triple = "x86_64-pc-windows-msvc"

@cxx_parameters = private constant [3 x i64] zeroinitializer
declare dllimport void @RaiseException(i32, i32, i32, ptr)
declare dllimport void @ExitProcess(i32)
declare i32 @__CxxFrameHandler3(...)
define void @tl_destructor() {
  ret void
}

define void @tl_entry() personality ptr @__CxxFrameHandler3 {
entry:
  invoke void @tl_destructor()
          to label %invoke_cont unwind label %lpad_catch

invoke_cont:
  %parameters = getelementptr inbounds [3 x i64], ptr @cxx_parameters, i32 0, i32 0
  invoke void @RaiseException(i32 3765269347, i32 0, i32 3, ptr %parameters)
          to label %invoke_cont_2 unwind label %lpad_cleanup

invoke_cont_2:
  call void @tl_destructor()
  call void @ExitProcess(i32 1)
  unreachable

lpad_cleanup:
  %cleanup_pad = cleanuppad within none []
  call void @tl_destructor()
  cleanupret from %cleanup_pad unwind label %lpad_catch

lpad_catch:
  %catch_switch = catchswitch within none [label %catch] unwind to caller

catch:
  %catch_pad = catchpad within %catch_switch [ptr null, i32 64, ptr null]
  catchret from %catch_pad to label %caught

caught:
  call void @ExitProcess(i32 0)
  unreachable
}
