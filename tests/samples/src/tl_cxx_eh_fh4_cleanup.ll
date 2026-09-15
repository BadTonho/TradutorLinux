; Fixture PE32+ mínima para executar duas ações do UnwindMap FH4. O mapa é
; preenchido pelo gerador após a ligação, pois seus destinos dependem dos RVAs
; finais da imagem.
target triple = "x86_64-pc-windows-msvc"

@cxx_parameters = private constant [3 x i64] zeroinitializer
@fh4_cleanup_marker = global i32 0
@fh4_metadata_blob = global [512 x i8] zeroinitializer, section ".data"
@fh4_metadata_magic = global [19 x i8] c"TLFH4_CLEANUP_BLOB\00"
@fh4_metadata_anchor = global i64 0

declare dllimport void @RaiseException(i32, i32, i32, ptr)
declare dllimport void @ExitProcess(i32)
declare i32 @__GSHandlerCheck_EH4(...)

define void @tl_fh4_dtor_with_object(ptr %object) {
entry:
  %object_is_valid = icmp ne ptr %object, null
  br i1 %object_is_valid, label %mark, label %fail
mark:
  store i32 1, ptr @fh4_cleanup_marker
  ret void
fail:
  call void @ExitProcess(i32 2)
  unreachable
}

define void @tl_fh4_dtor_rva() {
entry:
  store i32 3, ptr @fh4_cleanup_marker
  ret void
}

define void @tl_fh4_catch() {
entry:
  %marker = load i32, ptr @fh4_cleanup_marker
  %cleaned = icmp eq i32 %marker, 3
  %exit_code = select i1 %cleaned, i32 0, i32 1
  call void @ExitProcess(i32 %exit_code)
  unreachable
}

define void @tl_entry() personality ptr @__GSHandlerCheck_EH4 {
entry:
  %blob_byte = load volatile i8, ptr @fh4_metadata_blob
  %blob_value = zext i8 %blob_byte to i64
  store volatile i64 %blob_value, ptr @fh4_metadata_anchor
  invoke void @RaiseException(i32 3765269347, i32 0, i32 3, ptr @cxx_parameters)
          to label %normal unwind label %lpad
normal:
  call void @ExitProcess(i32 3)
  unreachable
lpad:
  %catch_switch = catchswitch within none [label %catch] unwind label %terminate
catch:
  %catch_pad = catchpad within %catch_switch [ptr null, i32 64, ptr null]
  catchret from %catch_pad to label %caught
terminate:
  %terminate_pad = cleanuppad within none []
  call void @ExitProcess(i32 4)
  unreachable
caught:
  call void @tl_fh4_catch()
  unreachable
}
