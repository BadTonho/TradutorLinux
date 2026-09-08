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
  invoke void @RaiseException(i32 3765269347, i32 0, i32 3, ptr @cxx_parameters)
          to label %normal unwind label %lpad_catch

normal:
  call void @tl_destructor()
  call void @ExitProcess(i32 1)
  unreachable

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
  call void @ExitProcess(i32 0)
  unreachable
}
